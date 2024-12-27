/*******************************************************************
 *
 *  ttraster.c                                                  1.5
 *
 *  The FreeType glyph rasterizer (body).
 *
 *  Copyright 1996-1999 by
 *  David Turner, Robert Wilhelm, and Werner Lemberg.
 *
 *  This file is part of the FreeType project, and may only be used
 *  modified and distributed under the terms of the FreeType project
 *  license, LICENSE.TXT. By continuing to use, modify, or distribute
 *  this file you indicate that you have read the license and
 *  understand and accept it fully.
 *
 *  NOTES:
 *
 *  This version supports the following:
 *
 *    - direct grayscaling
 *    - sub-banding
 *    - drop-out modes 4 and 5
 *    - second pass for complete drop-out control (bitmap only)
 *    - variable precision
 *
 *   Changes between 1.5 and 1.4:
 *
 *     Performance tuning.
 *
 *   Changes between 1.4 and 1.3:
 *
 *   Mainly performance tunings:
 *
 *   - Line_Down() and Bezier_Down() now use the functions Line_Up()
 *     and Bezier_Up() to do their work.
 *   - optimized Split_Bezier()
 *   - optimized linked lists used during sweeps
 *
 ******************************************************************/

#include "ttraster.h"
#include "tttypes.h"
#include "ttengine.h"
#include "ttcalc.h"      /* for TT_MulDiv only */

#include "ttmemory.h"    /* only used to allocate memory on engine init */


#ifdef __GEOS__
#include <geode.h>

extern TEngine_Instance engineInstance;
#endif  /* __GEOS__ */

/* required by the tracing mode */
#undef  TT_COMPONENT
#define TT_COMPONENT      trace_raster


/* render pool size */
#define  RASTER_RENDER_POOL_INITIAL     1
#define  RASTER_RENDER_POOL_FACTOR    256
#define  RASTER_RENDER_POOL_MIN_SIZE 1024


#define Raster_Err_None              TT_Err_Ok
#define Raster_Err_Not_Ini           TT_Err_Raster_Not_Initialized
#define Raster_Err_Overflow          TT_Err_Raster_Pool_Overflow
#define Raster_Err_Neg_Height        TT_Err_Raster_Negative_Height
#define Raster_Err_Invalid           TT_Err_Raster_Invalid_Value
#define Raster_Err_Gray_Unsupported  TT_Err_Raster_Gray_Unsupported


/* FMulDiv means "Fast MulDiv", it is uses in case where 'b' is typically */
/* a small value and the result of (a*b) is known to fit in 32 bits.      */
#define FMulDiv( a, b, c )  ( (a) * (b) / (c) )

/* On the other hand, SMulDiv is for "Slow MulDiv", and is used typically */
/* for clipping computations.  It simply uses the TT_MulDiv() function    */
/* defined in "ttcalc.h"                                                  */
/*                                                                        */
/* So, the following definition fits the bill nicely, and we don't need   */
/* to use the one in 'ttcalc' anymore, even for 16-bit systems...         */
#define SMulDiv   TT_MulDiv


/* Define DEBUG_RASTER if you want to generate a debug version of the  */
/* rasterizer.  This will progressively draw the glyphs while all the  */
/* computation are done directly on the graphics screen (the glyphs    */
/* will be inverted).                                                  */

/* Note that DEBUG_RASTER should only be used for debugging with b/w   */
/* rendering, not with gray levels.                                    */

/* The definition of DEBUG_RASTER should appear in the file            */
/* "ttconfig.h".                                                       */

#ifdef DEBUG_RASTER
  extern Char*  Vio;  /* A pointer to VRAM or display buffer */
#endif


#define LOCK_RENDER_POOL    Lock_Render_Pool( RAS_VARS  glyph )
#define UNLOCK_RENDER_POOL  MemUnlock( ras.buffer )


/* The rasterizer is a very general purpose component, please leave */
/* the following redefinitions there (you never know your target    */
/* environment).                                                    */

#ifndef TRUE
#define TRUE   1
#endif

#ifndef FALSE
#define FALSE  0
#endif

#ifndef NULL
#define NULL  (void*)0
#endif

#define MaxBezier  32   /* The maximum number of stacked Bezier curves. */
                        /* Setting this constant to more than 32 is a   */
                        /* pure waste of space.                         */

#define Pixel_Bits  6   /* fractional bits of *input* coordinates */

  /* States of each line, arc and profile */
  enum  TStates_
  {
    Unknown,
    Ascending,
    Descending,
    Flat
  };

  typedef enum TStates_  TStates;

  struct  TProfile_;
  typedef struct TProfile_  TProfile;
  typedef TProfile*         PProfile;

  struct  TProfile_
  {
    TT_F26Dot6  X;           /* current coordinate during sweep          */
    PProfile    link;        /* link to next profile - various purpose   */
    PStorage    offset;      /* start of profile's data in render pool   */
    Int         flow;        /* Profile orientation: Asc/Descending      */
    Short       height;      /* profile's height in scanlines            */
    Short       start;       /* profile's starting scanline              */

    UShort      countL;      /* number of lines to step before this      */
                             /* profile becomes drawable                 */

    PProfile    next;        /* next profile in same contour, used       */
                             /* during drop-out control                  */
  };

  typedef PProfile   TProfileList;
  typedef PProfile*  PProfileList;


  /* I use the classic trick of two dummy records for the head and tail  */
  /* of a linked list; this reduces tests in insertion/deletion/sorting. */
  /* NOTE: used during sweeps only.                                      */

  /* Simple record used to implement a stack of bands, required */
  /* by the sub-banding mechanism                               */

  struct  TBand_
  {
    Short  y_min;   /* band's minimum */
    Short  y_max;   /* band's maximum */
  };

  typedef struct TBand_  TBand;


#define AlignProfileSize \
          (( sizeof(TProfile)+sizeof(long)-1 ) / sizeof(long))


  /* Left fill bitmask */
  static const Byte  LMask[8] =
    { 0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01 };

  /* Right fill bitmask */
  static const Byte  RMask[8] =
    { 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF };

  /* prototypes used for sweep function dispatch */
#ifdef TT_CONFIG_OPTION_GRAY_SCALING
  typedef void  Function_Sweep_Init( RAS_ARGS Short*  min,
                                              Short*  max );
#else
  typedef void  Function_Sweep_Init( RAS_ARGS Short*  min );
#endif

#ifdef TT_CONFIG_OPTION_GRAY_SCALING
  typedef void  Function_Sweep_Span( RAS_ARGS Short       y,
                                              TT_F26Dot6  x1,
                                              TT_F26Dot6  x2,
                                              PProfile    left,
                                              PProfile    right );
#else
  typedef void  Function_Sweep_Span( RAS_ARGS Short       y,
                                              TT_F26Dot6  x1,
                                              TT_F26Dot6  x2 );
#endif

  typedef void  Function_Sweep_Drop( RAS_ARGS Short       y,
                                              TT_F26Dot6  x1,
                                              TT_F26Dot6  x2,
                                              PProfile    left,
                                              PProfile    right );

  typedef void  Function_Sweep_Step( RAS_ARGS Short y );


/* NOTE: These operations are only valid on 2's complement processors */

#define FLOOR( x )    ( (x) & -ras.precision )
#define CEILING( x )  ( ((x) + ras.precision - 1) & -ras.precision )
#define TRUNC( x )    ( (signed long)(x) >> ras.precision_bits )
#define FRAC( x )     ( (x) & (ras.precision - 1) )
#define SCALED( x )   ( ((x) << ras.precision_shift) - ras.precision_half )

#ifdef DEBUG_RASTER
#define DEBUG_PSET  Pset()
#else
#define DEBUG_PSET
#endif

  struct  TPoint_
  {
    Long  x, y;
  };

  typedef struct TPoint_  TPoint;


  /* Note that I have moved the location of some fields in the */
  /* structure to ensure that the most used variables are used */
  /* at the top.  Thus, their offset can be coded with less    */
  /* opcodes, and it results in a smaller executable.          */

  struct  TRaster_Instance_
  {
    Int       precision_bits;       /* precision related variables */
    Int       precision;
    Int       precision_half;
    Int       precision_shift;
    Int       precision_step;

    MemHandle buffer;               /* The profiles bufferblock     */
    PStorage  sizeBuff;             /* Render pool size             */
    PStorage  maxBuff;              /* Profiles buffer size         */
    PStorage  top;                  /* Current cursor in buffer     */

    TT_Error  error;

    PByte     flags;                /* current flags table    */
    PUShort   outs;                 /* current outlines table */

    UShort    nPoints;              /* number of points in current glyph   */
    Short     nContours;            /* number of contours in current glyph */
    Int       numTurns;             /* number of Y-turns in outline        */

    TPoint*   arc;                  /* current Bezier arc pointer */

    UShort    bWidth;               /* target bitmap width  */
    PByte     bTarget;              /* target bitmap or region buffer */

    Long      lastX, lastY, minY, maxY;

    UShort    num_Profs;            /* current number of profiles */

    Bool      fresh;                /* signals a fresh new profile which */
                                    /* 'start' field must be completed   */
    Bool      joint;                /* signals that the last arc ended   */
                                    /* exactly on a scanline.  Allows    */
                                    /* removal of doublets               */
    PProfile  cProfile;             /* current profile                   */
    PProfile  fProfile;             /* head of linked list of profiles   */
    PProfile  gProfile;             /* contour's first profile in case   */
                                    /* of impact                         */
    TStates   state;                /* rendering state */

    TT_Raster_Map  target;          /* description of target bit/pixmap */

    Short     traceOfs;             /* current offset in target bitmap */
    Short     traceOfsLastLine;     /* offset in traget region before line step */
    Short     traceIncr;            /* sweep's increment in target bitmap */

    /* dispatch variables */

    Function_Sweep_Init _near *    Proc_Sweep_Init;
    Function_Sweep_Span _near *    Proc_Sweep_Span;
    Function_Sweep_Drop _near *    Proc_Sweep_Drop;
    Function_Sweep_Step _near *    Proc_Sweep_Step;

    TT_Vector*  coords;

    Byte      dropOutControl;       /* current drop_out control method */

    TPoint    arcs[2 * MaxBezier + 1];      /* The Bezier stack */

    TBand     band_stack[16];       /* band stack used for sub-banding */
  };


#ifdef TT_CONFIG_OPTION_STATIC_RASTER

  static TRaster_Instance  cur_ras;
#define ras  cur_ras

#else

#define ras  (*raster)

#endif /* TT_STATIC_RASTER */


/************************************************************************/
/*                                                                      */
/* Function:    Set_Resolution                                          */
/*                                                                      */
/* Description: Sets precision variables according to param flag.       */
/*                                                                      */
/* Input:       High     set to True for high precision (typically for  */
/*                       ppem < 18), false otherwise.                   */
/*                                                                      */
/************************************************************************/

  static void _near  Set_Resolution( RAS_ARGS TT_UShort  y_ppem )
  {
    if ( y_ppem < 24 )
    {
      ras.precision_bits   = 10;
      ras.precision_step   = 128;
    }
    else
    {
      ras.precision_bits   = 6;
      ras.precision_step   = 32;
    }

    ras.precision       = 1 << ras.precision_bits;
    ras.precision_half  = ras.precision >> 1;
    ras.precision_shift = ras.precision_bits - Pixel_Bits;
  }


/****************************************************************************/
/*                                                                          */
/* Function:    New_Profile                                                 */
/*                                                                          */
/* Description: Creates a new Profile in the render pool.                   */
/*                                                                          */
/* Input:       aState   state/orientation of the new Profile               */
/*                                                                          */
/* Returns:     SUCCESS on success.                                         */
/*              FAILURE in case of overflow or of incoherent Profile.       */
/*                                                                          */
/****************************************************************************/

  static Bool _near  New_Profile( RAS_ARGS TStates  aState )
  {
    if ( !ras.fProfile )
    {
      ras.cProfile  = (PProfile)ras.top;
      ras.fProfile  = ras.cProfile;
      ras.top      += AlignProfileSize;
    }

    if ( ras.top >= ras.maxBuff )
    {
      ras.error = Raster_Err_Overflow;
      return FAILURE;
    }

    switch ( aState )
    {
    case Ascending:
      ras.cProfile->flow = TT_Flow_Up;
      break;

    case Descending:
      ras.cProfile->flow = TT_Flow_Down;
      break;

    default:
      ras.error = Raster_Err_Invalid;
      return FAILURE;
    }

    ras.cProfile->start  = 0;
    ras.cProfile->height = 0;
    ras.cProfile->offset = ras.top;
    ras.cProfile->link   = (PProfile)0;
    ras.cProfile->next   = (PProfile)0;

    if ( !ras.gProfile )
      ras.gProfile = ras.cProfile;

    ras.state = aState;
    ras.fresh = TRUE;
    ras.joint = FALSE;

    return SUCCESS;
  }


/****************************************************************************/
/*                                                                          */
/* Function:    End_Profile                                                 */
/*                                                                          */
/* Description: Finalizes the current Profile.                              */
/*                                                                          */
/* Input:       None                                                        */
/*                                                                          */
/* Returns:     SUCCESS on success.                                         */
/*              FAILURE in case of overflow or incoherency.                 */
/*                                                                          */
/****************************************************************************/

  static Bool _near  End_Profile( RAS_ARG )
  {
    Short     h;
    PProfile  oldProfile;


    h = ras.top - ras.cProfile->offset;

    if ( h < 0 )
    {
      ras.error = Raster_Err_Neg_Height;
      return FAILURE;
    }

    if ( h > 0 )
    {
      oldProfile           = ras.cProfile;
      ras.cProfile->height = h;
      ras.cProfile         = (PProfile)ras.top;

      ras.top             += AlignProfileSize;

      ras.cProfile->height = 0;
      ras.cProfile->offset = ras.top;
      oldProfile->next     = ras.cProfile;
      ++ras.num_Profs;
    }

    if ( ras.top >= ras.maxBuff )
    {
      ras.error = Raster_Err_Overflow;
      return FAILURE;
    }

    ras.joint = FALSE;

    return SUCCESS;
  }


/****************************************************************************/
/*                                                                          */
/* Function:    Insert_Y_Turn                                               */
/*                                                                          */
/* Description: Insert a salient into the sorted list placed on top         */
/*              of the render pool                                          */
/*                                                                          */
/* Input:       New y scanline position                                     */
/*                                                                          */
/****************************************************************************/

  static Bool _near  Insert_Y_Turn( RAS_ARGS  Int  y )
  {
    PStorage  y_turns;
    Int       y2, n;

    n       = ras.numTurns-1;
    y_turns = ras.sizeBuff - ras.numTurns;

    /* look for first y value that is <= */
    while ( n >= 0 && y < y_turns[n] )
      --n;

    /* if it is <, simply insert it, ignore if == */
    if ( n >= 0 && y > y_turns[n] )
      while ( n >= 0 )
      {
        y2 = y_turns[n];
        y_turns[n] = y;
        y = y2;
        --n;
      }

    if ( n < 0 )
    {
      if (ras.maxBuff <= ras.top)
      {
        ras.error = Raster_Err_Overflow;
        return FAILURE;
      }
      --ras.maxBuff;
      ++ras.numTurns;
      ras.sizeBuff[-ras.numTurns] = y;
    }

    return SUCCESS;
  }


/****************************************************************************/
/*                                                                          */
/* Function:    Finalize_Profile_Table                                      */
/*                                                                          */
/* Description: Adjusts all links in the Profiles list.                     */
/*                                                                          */
/* Input:       None                                                        */
/*                                                                          */
/* Returns:     None.                                                       */
/*                                                                          */
/****************************************************************************/

  static Bool _near  Finalize_Profile_Table( RAS_ARG )
  {
    Int       bottom, top;
    UShort    n;
    PProfile  p;


    n = ras.num_Profs;

    if ( n > 1 )
    {
      p = ras.fProfile;
      while ( n > 0 )
      {
        if ( n > 1 )
          p->link = (PProfile)( p->offset + p->height );
        else
          p->link = NULL;

        switch ( p->flow )
        {
        case TT_Flow_Down:
          bottom     = p->start - p->height+1;
          top        = p->start;
          p->start   = bottom;
          p->offset += p->height-1;
          break;

        case TT_Flow_Up:
        default:
          bottom = p->start;
          top    = p->start + p->height-1;
        }

        if ( Insert_Y_Turn( RAS_VARS  bottom ) ||
             Insert_Y_Turn( RAS_VARS  top+1 )  )
          return FAILURE;

        p = p->link;
        --n;
      }
    }
    else
      ras.fProfile = NULL;

    return SUCCESS;
  }


/****************************************************************************/
/*                                                                          */
/* Function:    Split_Bezier                                                */
/*                                                                          */
/* Description: Subdivides one Bezier arc into two joint                    */
/*              sub-arcs in the Bezier stack.                               */
/*                                                                          */
/* Input:       None (subdivided bezier is taken from the top of the        */
/*              stack).                                                     */
/*                                                                          */
/* Returns:     None.                                                       */
/*                                                                          */
/*                                                                          */
/* Note:  This routine is the 'beef' of this component. It is  _the_        */
/*        inner loop that should be optimized to hell to get the            */
/*        best performance.                                                 */
/*                                                                          */
/****************************************************************************/

  static void _near  Split_Bezier( TPoint*  base )
  {
    Long     a, b;


    base[4].x = base[2].x;
    b = base[1].x;
    a = base[3].x = ( base[2].x + b ) >> 1;
    b = base[1].x = ( base[0].x + b ) >> 1;
    base[2].x = ( a + b ) >> 1;

    base[4].y = base[2].y;
    b = base[1].y;
    a = base[3].y = ( base[2].y + b ) >> 1;
    b = base[1].y = ( base[0].y + b ) >> 1;
    base[2].y = ( a + b ) >> 1;

    /* hand optimized.  gcc doesn't seem too good at common expression */
    /* substitution and instruction scheduling ;-)                     */
  }


/****************************************************************************/
/*                                                                          */
/* Function:    Line_Up                                                     */
/*                                                                          */
/* Description: Computes the x-coordinates of an ascending line segment     */
/*              and stores them in the render pool.                         */
/*                                                                          */
/* Input:       x1,y1,x2,y2  Segment start (x1,y1) and end (x2,y2) points   */
/*                                                                          */
/* Returns:     SUCCESS on success.                                         */
/*              FAILURE on Render Pool overflow.                            */
/*                                                                          */
/****************************************************************************/

  static Bool _near  Line_Up( RAS_ARGS Long  x1, Long  y1,
                                 Long  x2, Long  y2,
                                 Long  miny, Long  maxy )
  {
    Long  Dx = x2 - x1, Dy = y2 - y1;
    Int   e1, e2, f1, f2, size;     /* XXX: is `Short' sufficient? */
    Long  Ix, Rx, Ax;

    PStorage  top;


    if ( Dy <= 0 || y2 < miny || y1 > maxy )
      return SUCCESS;

    if ( y1 < miny )
    {
      /* Take care : miny-y1 can be a very large value, we use     */
      /*             a slow MulDiv function to avoid clipping bugs */
      x1 += SMulDiv( Dx, miny - y1, Dy );
      e1  = TRUNC( miny );
      f1  = 0;
    }
    else
    {
      e1 = TRUNC( y1 );
      f1 = FRAC( y1 );
    }

    if ( y2 > maxy )
    {
      /* x2 += FMulDiv( Dx, maxy - y2, Dy );  UNNECESSARY */
      e2  = TRUNC( maxy );
      f2  = 0;
    }
    else
    {
      e2 = TRUNC( y2 );
      f2 = FRAC( y2 );
    }

    if ( f1 > 0 )
    {
      if ( e1 == e2 ) return SUCCESS;
      x1 += FMulDiv( Dx, ras.precision - f1, Dy );
      ++e1;
    }
    else if ( ras.joint )
      {
        ras.top--;
        ras.joint = FALSE;
      }

    ras.joint = ( f2 == 0 );

    if ( ras.fresh )
    {
      ras.cProfile->start = e1;
      ras.fresh           = FALSE;
    }

    size = e2 - e1 + 1;
    if ( ras.top + size >= ras.maxBuff )
    {
      ras.error = Raster_Err_Overflow;
      return FAILURE;
    }

    Ix = (ras.precision * Dx) / Dy;
    Rx = (ras.precision * Dx) % Dy;
    Dx = (Dx > 0) ? 1 : -1;

    Ax  = -Dy;
    top = ras.top;

    while ( size-- > 0 )
    {
      *top++ = x1;

      x1 += Ix;
      Ax += Rx;
      if ( Ax >= 0 )
      {
        Ax -= Dy;
        x1 += Dx;
      }
    }

    ras.top = top;
    return SUCCESS;
  }


  static Bool _near  Line_Down( RAS_ARGS Long  x1, Long  y1,
                                         Long  x2, Long  y2,
                                         Long  miny, Long  maxy )
  {
    Bool  fresh  = ras.fresh;
    Bool  result = Line_Up( RAS_VARS x1, -y1, x2, -y2, -maxy, -miny );

    if ( fresh && !ras.fresh )
      ras.cProfile->start = -ras.cProfile->start;

    return result;
  }


/****************************************************************************/
/*                                                                          */
/* Function:    Bezier_Up                                                   */
/*                                                                          */
/* Description: Computes thes x-coordinates of an ascending bezier arc      */
/*              and stores them in the render pool.                         */
/*                                                                          */
/* Input:       None.  The arc is taken from the top of the Bezier stack.   */
/*                                                                          */
/* Returns:     SUCCESS on success.                                         */
/*              FAILURE on Render Pool overflow.                            */
/*                                                                          */
/****************************************************************************/

  static Bool _near  Bezier_Up( RAS_ARGS Long  miny, Long  maxy )
  {
    Long     y1, y2, e, e2, e0;
    Short    f1;

    TPoint*  start_arc;
    TPoint*  arc = ras.arc;
    PStorage top = ras.top;


    y1  = arc[2].y;
    y2  = arc[0].y;

    if ( y2 < miny || y1 > maxy )
      goto Fin;

    e2 = FLOOR( y2 );

    if ( e2 > maxy )
      e2 = maxy;

    e0 = miny;

    if ( y1 < miny )
      e = miny;
    else
    {
      e  = CEILING( y1 );
      f1 = FRAC( y1 );
      e0 = e;

      if ( f1 == 0 )
      {
        if ( ras.joint )
        {
          --top;
          ras.joint = FALSE;
        }

        *top++ = arc[2].x;

        DEBUG_PSET;

        e += ras.precision;
      }
    }

    if ( ras.fresh )
    {
      ras.cProfile->start = TRUNC( e0 );
      ras.fresh = FALSE;
    }

    if ( e2 < e )
      goto Fin;

    if ( ( top + TRUNC( e2 - e ) + 1 ) >= ras.maxBuff )
    {
      ras.top   = top;
      ras.error = Raster_Err_Overflow;
      return FAILURE;
    }

    start_arc = arc;

    while ( arc >= start_arc && e <= e2 )
    {
      ras.joint = FALSE;

      y2 = arc[0].y;

      if ( y2 > e )
      {
        y1 = arc[2].y;
        if ( y2 - y1 >= ras.precision_step )
        {
          Split_Bezier( arc );
          arc += 2;
        }
        else
        {
          *top++ = arc[2].x + FMulDiv( arc[0].x - arc[2].x,
                                       e - y1,
                                       y2 - y1 );

          arc -= 2;
          e   += ras.precision;
        }
      }
      else
      {
        if ( y2 == e )
        {
          ras.joint  = TRUE;
          *top++     = arc[0].x;

          e += ras.precision;
        }
        arc -= 2;
      }
    }

  Fin:
    ras.top  = top;
    ras.arc -= 2;
    return SUCCESS;
  }


/****************************************************************************/
/*                                                                          */
/* Function:    Bezier_Down                                                 */
/*                                                                          */
/* Description: Computes the x-coordinates of a descending bezier arc       */
/*              and stores them in the render pool.                         */
/*                                                                          */
/* Input:       None.  Arc is taken from the top of the Bezier stack.       */
/*                                                                          */
/* Returns:     SUCCESS on success.                                         */
/*              FAILURE on Render Pool overflow.                            */
/*                                                                          */
/****************************************************************************/

  static Bool _near  Bezier_Down( RAS_ARGS Long  miny, Long  maxy )
  {
    TPoint*  arc = ras.arc;
    Bool     result, fresh;


    arc[0].y = -arc[0].y;
    arc[1].y = -arc[1].y;
    arc[2].y = -arc[2].y;

    fresh = ras.fresh;

    result = Bezier_Up( RAS_VARS -maxy, -miny );

    if ( fresh && !ras.fresh )
      ras.cProfile->start = -ras.cProfile->start;

    arc[0].y = -arc[0].y;
    return result;
  }


/****************************************************************************/
/*                                                                          */
/* Function:    Line_To                                                     */
/*                                                                          */
/* Description: Injects a new line segment and adjusts Profiles list.       */
/*                                                                          */
/* Input:       x, y : segment endpoint (start point in LastX,LastY)        */
/*                                                                          */
/* Returns:     SUCCESS on success.                                         */
/*              FAILURE on Render Pool overflow or Incorrect Profile.       */
/*                                                                          */
/****************************************************************************/

  static Bool _near  Line_To( RAS_ARGS Long  x, Long  y )
  {
    /* First, detect a change of direction */

    switch ( ras.state )
    {
    case Unknown:
      if ( y > ras.lastY )
      {
        if ( New_Profile( RAS_VARS  Ascending ) ) return FAILURE;
      } 
      else if ( y < ras.lastY )
      {
          if ( New_Profile( RAS_VARS  Descending ) ) return FAILURE;
      }
      break;

    case Ascending:
      if ( y < ras.lastY )
      {
        if ( End_Profile( RAS_VAR ) ||
             New_Profile( RAS_VARS  Descending ) ) return FAILURE;
      }
      break;

    case Descending:
      if ( y > ras.lastY )
      {
        if ( End_Profile( RAS_VAR ) ||
             New_Profile( RAS_VARS  Ascending ) ) return FAILURE;
      }
      break;
    }

    /* Then compute the lines */

    if ( ras.state == Ascending )
    {
      if ( Line_Up ( RAS_VARS  ras.lastX, ras.lastY,
                     x, y, ras.minY, ras.maxY ) )
        return FAILURE;
    }
    else if ( ras.state == Descending )
    {
      if ( Line_Down( RAS_VARS ras.lastX, ras.lastY,
                      x, y, ras.minY, ras.maxY ) )
        return FAILURE;
    }

    ras.lastX = x;
    ras.lastY = y;

    return SUCCESS;
  }


/****************************************************************************/
/*                                                                          */
/* Function:    Bezier_To                                                   */
/*                                                                          */
/* Description: Injects a new bezier arc and adjusts the profile list.      */
/*                                                                          */
/* Input:       x,   y : arc endpoint (start point in LastX, LastY)         */
/*              Cx, Cy : control point                                      */
/*                                                                          */
/* Returns:     SUCCESS on success.                                         */
/*              FAILURE on Render Pool overflow or Incorrect Profile.       */
/*                                                                          */
/****************************************************************************/

  static Bool _near  Bezier_To( RAS_ARGS Long  x,
                                   Long  y,
                                   Long  cx,
                                   Long  cy )
  {
    Long     y1, y2, y3, x3;
    TStates  state_bez;


    ras.arc      = ras.arcs;
    ras.arc[2].x = ras.lastX; ras.arc[2].y = ras.lastY;
    ras.arc[1].x = cx;        ras.arc[1].y = cy;
    ras.arc[0].x = x;         ras.arc[0].y = y;

    do
    {
      y1 = ras.arc[2].y;
      y2 = ras.arc[1].y;
      y3 = ras.arc[0].y;
      x3 = ras.arc[0].x;

      /* first, categorize the bezier arc */

      if ( y1 == y2 )
      {
        if ( y2 == y3 )
          state_bez = Flat;
        else if ( y2 > y3 )
          state_bez = Descending;
        else
          state_bez = Ascending;
      }
      else if ( y1 > y2 )
      {
        if ( y2 >= y3 )
          state_bez = Descending;
        else
          state_bez = Unknown;
      }
      else if ( y2 <= y3 )
        state_bez = Ascending;
      else
        state_bez = Unknown;

      /* split non-monotonic arcs, ignore flat ones, or */
      /* computes the up and down ones                  */

      switch ( state_bez )
      {
      case Flat:
        ras.arc -= 2;
        break;

      case Unknown:
        Split_Bezier( ras.arc );
        ras.arc += 2;
        break;

      default:
        /* detect a change of direction */

        if ( ras.state != state_bez )
        {
          if ( ras.state != Unknown )
            if ( End_Profile( RAS_VAR ) ) return FAILURE;

          if ( New_Profile( RAS_VARS state_bez ) ) return FAILURE;
        }

        /* compute */

        switch ( ras.state )
        {
        case Ascending:
          if ( Bezier_Up ( RAS_VARS ras.minY, ras.maxY ) )
            return FAILURE;
          break;

        case Descending:
          if ( Bezier_Down( RAS_VARS ras.minY, ras.maxY ) )
            return FAILURE;
          break;
        }
      }
    } while ( ras.arc >= ras.arcs );

    ras.lastX = x3;
    ras.lastY = y3;

    return SUCCESS;
  }


/****************************************************************************/
/*                                                                          */
/* Function:    Decompose_Curve                                             */
/*                                                                          */
/* Description: Scans the outline arays in order to emit individual         */
/*              segments and beziers by calling Line_To() and Bezier_To().  */
/*              It handles all weird cases, like when the first point       */
/*              is off the curve, or when there are simply no 'on'          */
/*              points in the contour!                                      */
/*                                                                          */
/* Input:       first, last    : indexes of first and last point in         */
/*                               contour.                                   */
/*                                                                          */
/* Returns:     SUCCESS on success.                                         */
/*              FAILURE on error.                                           */
/*                                                                          */
/****************************************************************************/

#undef  SWAP_
#define SWAP_(x,y)  { Long swap = x; x = y; y = swap; }

  static Bool _near  Decompose_Curve( RAS_ARGS UShort  first,
                                               UShort  last,
                                               Bool    flipped )
  {
    Long   x,  y;   /* current point                */
    Long   cx, cy;  /* current Bezier control point */
    Long   mx, my;  /* current middle point         */

    Long   x_first, y_first;  /* first point's coordinates */
    Long   x_last,  y_last;   /* last point's coordinates  */

    UShort  index;     /* current point's index */
    Bool    on_curve;  /* current point's state */

    x_first = SCALED( ras.coords[first].x );
    y_first = SCALED( ras.coords[first].y );

    if ( flipped ) SWAP_( x_first,y_first );

    x_last  = SCALED( ras.coords[last].x );
    y_last  = SCALED( ras.coords[last].y );

    if ( flipped ) SWAP_( x_last,y_last );

    ras.lastX = cx = x_first;
    ras.lastY = cy = y_first;

    on_curve = (ras.flags[first] & 1);
    index    = first;

    /* check first point to determine origin */
    if ( !on_curve )
    {
      /* first point is off the curve.  Yes, this happens... */
      if ( ras.flags[last] & 1 )
      {
        ras.lastX = x_last;  /* start at last point if it */
        ras.lastY = y_last;  /* is on the curve           */
      }
      else
      {
        /* if both first and last points are off the curve, */
        /* start at their middle and record its position    */
        /* for closure                                      */
        ras.lastX = (ras.lastX + x_last) >> 1;
        ras.lastY = (ras.lastY + y_last) >> 1;

        x_last = ras.lastX;
        y_last = ras.lastY;
      }
    }

    /* now process each contour point individually */
    while ( index < last )
    {
      ++index;
      x = SCALED( ras.coords[index].x );
      y = SCALED( ras.coords[index].y );

      if ( flipped ) SWAP_( x, y );

      if ( on_curve )
      {
        /* the previous point was on the curve */
        on_curve = ( ras.flags[index] & 1 );
        if ( on_curve )
        {
          /* two successive on points => emit segment */
          if ( Line_To( RAS_VARS  x, y ) ) return FAILURE;
        }
        else
        {
          /* else, keep current control point for next bezier */
          cx = x;
          cy = y;
        }
      }
      else
      {
        /* the previous point was off the curve */
        on_curve = ( ras.flags[index] & 1 );
        if ( on_curve )
        {
          /* reaching an `on' point */
          if ( Bezier_To( RAS_VARS  x, y, cx, cy ) ) return FAILURE;
        }
        else
        {
          /* two successive `off' points => create middle point */
          mx = ( cx + x ) >> 1;
          my = ( cy + y ) >> 1;

          if ( Bezier_To( RAS_VARS  mx, my, cx, cy ) ) return FAILURE;

          cx = x;
          cy = y;
        }
      }
    }

    /* end of contour, close curve cleanly */
    if ( ras.flags[first] & 1 )
    {
      if ( on_curve )
        return Line_To( RAS_VARS  x_first, y_first );
      else
        return Bezier_To( RAS_VARS  x_first, y_first, cx, cy );
    }
    else
      if ( !on_curve )
        return Bezier_To( RAS_VARS  x_last, y_last, cx, cy );

    return SUCCESS;
  }


/****************************************************************************/
/*                                                                          */
/* Function:    Convert_Glyph                                               */
/*                                                                          */
/* Description: Converts a glyph into a series of segments and arcs         */
/*              and makes a Profiles list with them.                        */
/*                                                                          */
/* Input:       _xCoord, _yCoord : coordinates tables.                      */
/*                                                                          */
/*              Uses the 'Flag' table too.                                  */
/*                                                                          */
/* Returns:     SUCCESS on success.                                         */
/*              FAILURE if any error was encountered during rendering.      */
/*                                                                          */
/****************************************************************************/

  static Bool _near  Convert_Glyph( RAS_ARGS int  flipped )
  {
    Short     i;
    UShort    start = 0;
    PProfile  lastProfile;


    ras.fProfile         = NULL;
    ras.joint            = FALSE;
    ras.fresh            = FALSE;
    ras.maxBuff          = ras.sizeBuff - AlignProfileSize;
    ras.numTurns         = 0;
    ras.cProfile         = (PProfile)ras.top;
    ras.cProfile->offset = ras.top;
    ras.num_Profs        = 0;

    for ( i = 0; i < ras.nContours; ++i )
    {
      ras.state    = Unknown;
      ras.gProfile = NULL;

      if ( Decompose_Curve( RAS_VARS  start, ras.outs[i], flipped ) )
        return FAILURE;

      start = ras.outs[i] + 1;

      /* We must now see if the extreme arcs join or not */
      if ( ( FRAC( ras.lastY ) == 0 &&
             ras.lastY >= ras.minY  &&
             ras.lastY <= ras.maxY ) )
        if ( ras.gProfile && ras.gProfile->flow == ras.cProfile->flow )
          --ras.top;
        /* Note that ras.gProfile can be nil if the contour was too small */
        /* to be drawn.                                                   */

      lastProfile = ras.cProfile;
      if ( End_Profile( RAS_VAR ) ) return FAILURE;

      /* close the 'next profile in contour' linked list */
      if ( ras.gProfile )
        lastProfile->next = ras.gProfile;
    }

    if (Finalize_Profile_Table( RAS_VAR ))
      return FAILURE;

    return (ras.top < ras.maxBuff ? SUCCESS : FAILURE );
  }


/************************************************/
/*                                              */
/*  InsNew :                                    */
/*                                              */
/*    Inserts a new Profile in a linked list.   */
/*                                              */
/************************************************/

  static void _near  InsNew( PProfileList  list,
                             PProfile      profile )
  {
    PProfile* insert_point = list;
    PProfile  current      = *insert_point;


    while (current && current->X < profile->X) {
        insert_point = &current->link;
        current = *insert_point;
    }

    profile->link = current;
    *insert_point = profile;
  }


/*************************************************/
/*                                               */
/*  DelOld :                                     */
/*                                               */
/*    Removes an old Profile from a linked list. */
/*                                               */
/*************************************************/

  static void _near  DelOld( PProfileList  list,
                             PProfile      profile )
  {
    PProfile* previous = list;
    PProfile current = *previous;


    while (current) {
        if (current == profile) {
            *previous = current->link;
            return;
        }

        previous = &current->link;
        current = *previous;
    }

    /* we should never get there, unless the Profile was not part of */
    /* the list.                                                     */
  }


/************************************************/
/*                                              */
/*  Update :                                    */
/*                                              */
/*    Update all X offsets of a drawing list    */
/*                                              */
/************************************************/

  static void _near  Update( PProfile  first )
  {
    while (first) {
        first->X = *first->offset;
        first->offset += first->flow;
        first->height--;
        first = first->link;
    }
  }


/************************************************/
/*                                              */
/*  Sort :                                      */
/*                                              */
/*    Sorts a trace list.  In 95%, the list     */
/*    is already sorted.  We need an algorithm  */
/*    which is fast in this case.  Bubble sort  */
/*    is enough and simple.                     */
/*                                              */
/************************************************/

  static void _near  Sort( PProfileList  list )
  {
    PProfile  *old, current, next;


    /* First, set the new X coordinate of each profile */
    Update( *list );

    /* Then sort them */
    old     = list;
    current = *old;

    if ( !current )
      return;

    next = current->link;

    while ( next )
    {
      if ( current->X <= next->X )
      {
        old     = &current->link;
        current = *old;

        if ( !current )
          return;
      }
      else
      {
        *old          = next;
        current->link = next->link;
        next->link    = current;

        old     = list;
        current = *old;
      }

      next = current->link;
    }
  }


/***********************************************************************/
/*                                                                     */
/*  Vertical Sweep Procedure Set :                                     */
/*                                                                     */
/*  These three routines are used during the vertical black/white      */
/*  sweep phase by the generic Draw_Sweep() function.                  */
/*                                                                     */
/***********************************************************************/

  static void _near  Vertical_Sweep_Init( RAS_ARGS Short*  min )
  {
    ras.traceOfs  = ( ras.target.rows - 1 - *min ) * ras.target.cols;
    ras.traceIncr = -ras.target.cols;
  }


  static void _near  Vertical_Sweep_Span( RAS_ARGS Short       y,
                                                   TT_F26Dot6  x1,
                                                   TT_F26Dot6  x2 )
  {
    Short  e1, e2;
    Short  c1, c2;
    Byte*  target;

    (void)y;


    /* Drop-out control */

    e1 = TRUNC( CEILING( x1 ) );

    if ( x2-x1-ras.precision <= 1 )
      e2 = e1;
    else
      e2 = TRUNC( FLOOR( x2 ) );

    if ( e2 >= 0 && e1 < ras.bWidth )
    {
      if ( e1 < 0 )           e1 = 0;
      if ( e2 >= ras.bWidth ) e2 = ras.bWidth-1;

      c1 = (Short)(e1 >> 3);
      c2 = (Short)(e2 >> 3);

      target = ras.bTarget + ras.traceOfs + c1;

      if ( c1 != c2 )
      {
        *target |= LMask[e1 & 7];

        if ( c2 > c1 + 1 )
          MEM_Set( target + 1, 0xFF, c2 - c1 - 1 );

        target[c2 - c1] |= RMask[e2 & 7];
      }
      else
        *target |= ( LMask[e1 & 7] & RMask[e2 & 7] );
    }
  }


  static void _near  Vertical_Sweep_Drop( RAS_ARGS Short       y,
                                                   TT_F26Dot6  x1,
                                                   TT_F26Dot6  x2,
                                                   PProfile    left,
                                                   PProfile    right )
  {
    Short  e1, e2;


    /* Drop-out control */

    e1 = CEILING( x1 );
    e2 = FLOOR  ( x2 );

    if ( e1 > e2 )
    {
      if ( e1 == e2 + ras.precision )
      {
        switch ( ras.dropOutControl )
        {
        case 1:
          e1 = e2;
          break;

        case 4:
          e1 = CEILING( (x1 + x2 + 1) >> 1 );
          break;

        case 2:
        case 5:
          /* Drop-out Control Rule #4 */

          /* The spec is not very clear regarding rule #4.  It      */
          /* presents a method that is way too costly to implement  */
          /* while the general idea seems to get rid of 'stubs'.    */
          /*                                                        */
          /* Here, we only get rid of stubs recognized when:        */
          /*                                                        */
          /*  upper stub:                                           */
          /*                                                        */
          /*   - P_Left and P_Right are in the same contour         */
          /*   - P_Right is the successor of P_Left in that contour */
          /*   - y is the top of P_Left and P_Right                 */
          /*                                                        */
          /*  lower stub:                                           */
          /*                                                        */
          /*   - P_Left and P_Right are in the same contour         */
          /*   - P_Left is the successor of P_Right in that contour */
          /*   - y is the bottom of P_Left                          */
          /*                                                        */

          /* FIXXXME : uncommenting this line solves the disappearing */
          /*           bit problem in the '7' of verdana 10pts, but   */
          /*           makes a new one in the 'C' of arial 14pts      */

          /* if ( x2-x1 < ras.precision_half ) */
          {
            /* upper stub test */

            if ( left->next == right && left->height <= 0 ) return;

            /* lower stub test */

            if ( right->next == left && left->start == y ) return;
          }

          /* check that the rightmost pixel isn't set */

          e1 = TRUNC( e1 );

          if ( e1 >= 0 && e1 < ras.bWidth &&
               ras.bTarget[ras.traceOfs + (e1 >> 3)] & (0x80 >> ( e1 & 7 )))
            return;

          if ( ras.dropOutControl == 2 )
            e1 = e2;
          else
            e1 = CEILING( (x1 + x2 + 1) >> 1 );

          break;

        default:
          return;  /* unsupported mode */
        }
      }
      else
        return;
    }

    e1 = TRUNC( e1 );

    if ( e1 >= 0 && e1 < ras.bWidth )
      ras.bTarget[ras.traceOfs + (e1 >> 3)] |= (Char)(0x80 >> ( e1 & 7 ));
  }


  static void _near Vertical_Sweep_Step( RAS_ARGS Short y )
  {
    (void)y;

    ras.traceOfs += ras.traceIncr;
  }


#ifdef __GEOS__

/***********************************************************************/
/*                                                                     */
/*  Vertical Sweep Procedure Set for regions :                         */
/*                                                                     */
/*  These three routines are used during the vertical black/white      */
/*  sweep phase for regions by the generic Draw_Sweep() function.      */
/*                                                                     */
/***********************************************************************/

  static void _near  Vertical_Region_Sweep_Init( RAS_ARGS Short*  min )
  {
    (void)min;

    ras.traceOfs         = 0;
    ras.traceIncr        = 0;
    ras.traceOfsLastLine = -1;
  }

  static void _near  Vertical_Region_Sweep_Span( RAS_ARGS Short       y,
                                                          TT_F26Dot6  x1,
                                                          TT_F26Dot6  x2 )
  {
    Short   e1     = TRUNC( CEILING( x1 ) );
    Short   e2     = TRUNC( FLOOR( x2 ) );
    PShort  target = ( (PShort)ras.bTarget ) + ras.traceOfs;;


    if ( ras.traceIncr == 0 )
      target[ras.traceIncr++] = y;

    if ( e2 >= 0 && e1 < ras.bWidth )   
    {
      if ( e1 < 0 )           e1 = 0;
      if ( e2 >= ras.bWidth ) e2 = ras.bWidth-1;

      target[ras.traceIncr++] = ( Short ) e1;
      target[ras.traceIncr++] = ( Short ) e2;
    }
  }

  static void _near  Vertical_Region_Sweep_Drop( RAS_ARGS Short       y,
                                                          TT_F26Dot6  x1,
                                                          TT_F26Dot6  x2,
                                                          PProfile    left,
                                                          PProfile    right )
  {
    (void)raster, (void)y, (void)x1, (void)x2, (void)left, (void)right;
  } 

  static void _near  Vertical_Region_Sweep_Step( RAS_ARGS Short y )
  {
    PShort  target         = ( (PShort)ras.bTarget ) + ras.traceOfs;
    PShort  targetLastLine = ( (PShort)ras.bTarget ) + ras.traceOfsLastLine;


    /* special case: the current line was empty */

    if ( ras.traceIncr == 0 )
      target[ras.traceIncr++] = y;
      

    /* finish current line */

    target[ras.traceIncr++] = (Short)EOREGREC;


    /* special case: no differences between last and current line */

    if ( ( ras.traceOfsLastLine > -1 ) &&
         ( MEM_Cmp( targetLastLine + 1, target + 1, ( ras.traceIncr - 1 ) * sizeof( Short ) ) == 0 ) )
    {
      *targetLastLine = *target;
      ras.traceIncr   = 0;
      return;
    }   


    /* move to the next line */

    ras.traceOfsLastLine = ras.traceOfs;
    ras.traceOfs         += ras.traceIncr;
    ras.traceIncr        = 0;
  }

  static void _near  Region_Sweep_Finish( RAS_ARG )
  {
    Short*  target = ( (PShort)ras.bTarget ) + ras.traceOfs;


    target[ras.traceIncr++] = (Short)EOREGREC;
    ras.target.size = ( ras.traceOfs + ras.traceIncr ) * sizeof( Short );
  }

#endif  /* __GEOS__ */


/***********************************************************************/
/*                                                                     */
/*  Horizontal Sweep Procedure Set :                                   */
/*                                                                     */
/*  These three routines are used during the horizontal black/white    */
/*  sweep phase by the generic Draw_Sweep() function.                  */
/*                                                                     */
/***********************************************************************/

  static void _near  Horizontal_Sweep_Init( RAS_ARGS Short*  min )
  {
    (void)raster, (void)min;
  }


  static void _near  Horizontal_Sweep_Span( RAS_ARGS Short y,
                                                     TT_F26Dot6  x1,
                                                     TT_F26Dot6  x2 )
  {
    Short  e1, e2;
    PByte  bits;


    if ( x2-x1 < ras.precision )
    {
      e1 = CEILING( x1 );
      e2 = FLOOR  ( x2 );

      if ( e1 == e2 )
      {
        e1 = TRUNC( e1 );

        if ( e1 >= 0 && e1 < ras.target.rows )
        {
          bits = ras.bTarget + (y >> 3);
          bits[(ras.target.rows-1 - e1) * ras.target.cols] |= ((Byte)(0x80 >> (y  & 7)));
        }
      }
    }
  }


  static void _near  Horizontal_Sweep_Drop( RAS_ARGS Short y,
                                                     TT_F26Dot6  x1,
                                                     TT_F26Dot6  x2,
                                                     PProfile    left,
                                                     PProfile    right )
  {
    Short  e1 = CEILING( x1 );
    Short  e2 = FLOOR  ( x2 );
    PByte  bits;


    /* During the horizontal sweep, we only take care of drop-outs */

    if ( e1 > e2 )
    {
      if ( e1 == e2 + ras.precision )
      {
        switch ( ras.dropOutControl )
        {
        case 1:
          e1 = e2;
          break;

        case 4:
          e1 = CEILING( (x1 + x2 + 1) >> 1 );
          break;

        case 2:
        case 5:

          /* Drop-out Control Rule #4 */

          /* The spec is not very clear regarding rule #4.  It      */
          /* presents a method that is way too costly to implement  */
          /* while the general idea seems to get rid of 'stubs'.    */
          /*                                                        */

          /* rightmost stub test */

          if ( left->next == right && left->height <= 0 ) return;

          /* leftmost stub test */

          if ( right->next == left && left->start == y ) return;

          /* check that the rightmost pixel isn't set */

          e1 = TRUNC( e1 );

          bits = ras.bTarget + (y >> 3);

          bits += (ras.target.rows-1-e1) * ras.target.cols;

          if ( e1 >= 0              &&
               e1 < ras.target.rows &&
               *bits & ((Byte)(0x80 >> (y &  7))) )
            return;

          if ( ras.dropOutControl == 2 )
            e1 = e2;
          else
            e1 = CEILING( (x1 + x2 + 1) >> 1 );

          break;

        default:
          return;  /* unsupported mode */
        }
      }
      else
        return;
    }

    bits = ras.bTarget + (y >> 3);

    e1 = TRUNC( e1 );

    if ( e1 >= 0 && e1 < ras.target.rows )
        bits[(ras.target.rows-1-e1) * ras.target.cols] |= (Byte)(0x80 >> (y  & 7));
  }


  static void _near Horizontal_Sweep_Step( RAS_ARGS Short y )
  {
    (void)raster, (void) y;
  }


#ifdef TT_CONFIG_OPTION_GRAY_SCALING

/***********************************************************************/
/*                                                                     */
/*  Vertical Gray Sweep Procedure Set:                                 */
/*                                                                     */
/*  These two routines are used during the vertical gray-levels        */
/*  sweep phase by the generic Draw_Sweep() function.                  */
/*                                                                     */
/*                                                                     */
/*  NOTES:                                                             */
/*                                                                     */
/*  - The target pixmap's width *must* be a multiple of 4.             */
/*                                                                     */
/*  - you have to use the function Vertical_Sweep_Span() for           */
/*    the gray span call.                                              */
/*                                                                     */
/***********************************************************************/

  static void  Vertical_Gray_Sweep_Init( RAS_ARGS Short*  min, Short*  max )
  {
    *min = *min & -2;
    *max = ( *max + 3 ) & -2;

    ras.traceOfs = 0;

    switch ( ras.target.flow )
    {
    case TT_Flow_Up:
      ras.traceG  = (*min / 2) * ras.target.cols;
      ras.traceIncr = ras.target.cols;
      break;

    default:
      ras.traceG    = (ras.target.rows-1 - *min/2) * ras.target.cols;
      ras.traceIncr = -ras.target.cols;
    }

    ras.gray_min_x =  ras.target.cols;
    ras.gray_max_x = -ras.target.cols;
  }


  static void  Vertical_Gray_Sweep_Step( RAS_ARG )
  {
    Int    c1, c2;
    PByte  pix, bit, bit2;
    Int*   count = ras.count_table;
    Byte*  grays;


    ras.traceOfs += ras.gray_width;

    if ( ras.traceOfs > ras.gray_width )
    {
      pix   = ras.gTarget + ras.traceG + ras.gray_min_x * 4;
      grays = ras.grays;

      if ( ras.gray_max_x >= 0 )
      {
        if ( ras.gray_max_x >= ras.target.width )
          ras.gray_max_x = ras.target.width-1;

        if ( ras.gray_min_x < 0 )
          ras.gray_min_x = 0;

        bit   = ras.bTarget + ras.gray_min_x;
        bit2  = bit + ras.gray_width;

        c1 = ras.gray_max_x - ras.gray_min_x;

        while ( c1 >= 0 )
        {
          c2 = count[*bit] + count[*bit2];

          if ( c2 )
          {
            pix[0] = grays[(c2 & 0xF000) >> 12];
            pix[1] = grays[(c2 & 0x0F00) >>  8];
            pix[2] = grays[(c2 & 0x00F0) >>  4];
            pix[3] = grays[(c2 & 0x000F)      ];

            *bit  = 0;
            *bit2 = 0;
          }

          bit ++;
          bit2++;
          pix += 4;
          c1  --;
        }
      }

      ras.traceOfs = 0;
      ras.traceG  += ras.traceIncr;

      ras.gray_min_x =  ras.target.cols;
      ras.gray_max_x = -ras.target.cols;
    }
  }


  static void  Horizontal_Gray_Sweep_Span( RAS_ARGS Short       y,
                                                    TT_F26Dot6  x1,
                                                    TT_F26Dot6  x2,
                                                    PProfile    left,
                                                    PProfile    right )
  {
    /* nothing, really */
  }

  static void  Horizontal_Gray_Sweep_Drop( RAS_ARGS Short       y,
                                                    TT_F26Dot6  x1,
                                                    TT_F26Dot6  x2,
                                                    PProfile    left,
                                                    PProfile    right )
  {
    Long  e1, e2;
    PByte pixel;
    Byte  color;


    /* During the horizontal sweep, we only take care of drop-outs */
    e1 = CEILING( x1 );
    e2 = FLOOR  ( x2 );

    if ( e1 > e2 )
    {
      if ( e1 == e2 + ras.precision )
      {
        switch ( ras.dropOutControl )
        {
        case 1:
          e1 = e2;
          break;

        case 4:
          e1 = CEILING( (x1 + x2 + 1) / 2 );
          break;

        case 2:
        case 5:

          /* Drop-out Control Rule #4 */

          /* The spec is not very clear regarding rule #4.  It      */
          /* presents a method that is way too costly to implement  */
          /* while the general idea seems to get rid of 'stubs'.    */
          /*                                                        */

          /* rightmost stub test */
          if ( left->next == right && left->height <= 0 ) return;

          /* leftmost stub test */
          if ( right->next == left && left->start == y ) return;

          if ( ras.dropOutControl == 2 )
            e1 = e2;
          else
            e1 = CEILING( (x1 + x2 + 1) / 2 );

          break;

        default:
          return;  /* unsupported mode */
        }
      }
      else
        return;
    }

    if ( e1 >= 0 )
    {
      if ( x2 - x1 >= ras.precision_half )
        color = ras.grays[2];
      else
        color = ras.grays[1];

      e1 = TRUNC( e1 ) / 2;
      if ( e1 < ras.target.rows )
      {
        if ( ras.target.flow == TT_Flow_Down )
          pixel = ras.gTarget +
                  (ras.target.rows - 1 - e1) * ras.target.cols + y / 2;
        else
          pixel = ras.gTarget +
                  e1 * ras.target.cols + y / 2;

        if (pixel[0] == ras.grays[0])
          pixel[0] = color;
      }
    }
  }

#endif /* TT_CONFIG_OPTION_GRAY_SCALING */


/********************************************************************/
/*                                                                  */
/*  Generic Sweep Drawing routine                                   */
/*                                                                  */
/********************************************************************/

  static Bool _near  Draw_Sweep( RAS_ARG )
  {
    Short  y, y_change, y_height;

    PProfile  P, Q, P_Left, P_Right;

    Short  min_Y, max_Y, top, bottom, dropouts;

    Long  x1, x2, xs;
    Short e1, e2;

    TProfileList  wait       = NULL;
    TProfileList  draw_left  = NULL;
    TProfileList  draw_right = NULL;


    /* first, compute min and max Y */

    P     = ras.fProfile;
    max_Y = (short)TRUNC( ras.minY );
    min_Y = (short)TRUNC( ras.maxY );

    while ( P )
    {
      Q = P->link;

      bottom = P->start;
      top    = P->start + P->height-1;

      if ( min_Y > bottom ) min_Y = bottom;
      if ( max_Y < top    ) max_Y = top;

      P->X = 0;
      InsNew( &wait, P );

      P = Q;
    }

    /* Check the Y-turns */
    if ( ras.numTurns == 0 )
    {
      ras.error = Raster_Err_Invalid;
      return FAILURE;
    }

    /* Now inits the sweep */

#ifdef TT_CONFIG_OPTION_GRAY_SCALING
    ras.Proc_Sweep_Init( RAS_VARS  &min_Y, &max_Y );
#else
    ras.Proc_Sweep_Init( RAS_VARS  &min_Y );
#endif

    /* Then compute the distance of each profile from min_Y */

    P = wait;

    while ( P )
    {
      P->countL = P->start - min_Y;
      P = P->link;
    }

    /* Let's go */

    y        = min_Y;
    y_height = 0;

    if ( ras.numTurns > 0 &&
         ras.sizeBuff[-ras.numTurns] == min_Y )
      --ras.numTurns;

    while ( ras.numTurns > 0 )
    {
      /* look in the wait list for new activations */

      P = wait;

      while ( P )
      {
        Q = P->link;
        P->countL -= y_height;
        if ( P->countL == 0 )
        {
          DelOld( &wait, P );

          switch ( P->flow )
          {
            case TT_Flow_Up:    InsNew( &draw_left,  P ); break;
            case TT_Flow_Down:  InsNew( &draw_right, P ); break;
          } 
        }

        P = Q;
      }

      /* Sort the drawing lists */

      Sort( &draw_left );
      Sort( &draw_right );

      y_change = (Short)ras.sizeBuff[-ras.numTurns--];
      y_height = y_change - y;

      while ( y < y_change )
      {

        /* Let's trace */

        dropouts = 0;

        P_Left  = draw_left;
        P_Right = draw_right;

        while ( P_Left )
        {
          x1 = P_Left ->X;
          x2 = P_Right->X;

          if ( x1 > x2 )
          {
            xs = x1;
            x1 = x2;
            x2 = xs;
          }

          if ( x2-x1 <= ras.precision )
          {
            e1 = FLOOR( x1 );
            e2 = CEILING( x2 );

            if ( ras.dropOutControl != 0 &&
                 (e1 > e2 || e2 == e1 + ras.precision) )
            {
              /* a drop out was detected */

              P_Left ->X = x1;
              P_Right->X = x2;

              /* mark profile for drop-out processing */
              P_Left->countL = 1;
              ++dropouts;

              goto Skip_To_Next;
            }
          }
#ifdef TT_CONFIG_OPTION_GRAY_SCALING
          ras.Proc_Sweep_Span( RAS_VARS  y, x1, x2, P_Left, P_Right );
#else
          ras.Proc_Sweep_Span( RAS_VARS  y, x1, x2 );
#endif

   Skip_To_Next:

          P_Left  = P_Left->link;
          P_Right = P_Right->link;
        }

        /* now perform the dropouts _after_ the span drawing   */
        /* drop-outs processing has been moved out of the loop */
        /* for performance tuning                              */
        if (dropouts > 0)
          goto Scan_DropOuts;

   Next_Line:

        ras.Proc_Sweep_Step( RAS_VARS y );

        ++y;

        if ( y < y_change )
        {
          Sort( &draw_left  );
          Sort( &draw_right );
        }

      }

      /* Now finalize the profiles that needs it */

      {
        PProfile  Q, P;
        P = draw_left;
        while ( P )
        {
          Q = P->link;
          if ( P->height == 0 )
            DelOld( &draw_left, P );
          P = Q;
        }
      }

      {
        PProfile  Q, P = draw_right;
        while ( P )
        {
          Q = P->link;
          if ( P->height == 0 )
            DelOld( &draw_right, P );
          P = Q;
        }
      }
    }

#ifdef TT_CONFIG_OPTION_GRAY_SCALING
    /* for gray-scaling, flushes the bitmap scanline cache */
    while ( y <= max_Y )
    {
      ras.Proc_Sweep_Step( RAS_VARS y );
      ++y;
    }
#endif

    return SUCCESS;

Scan_DropOuts :

    P_Left  = draw_left;
    P_Right = draw_right;

    while ( P_Left )
    {
      if ( P_Left->countL )
      {
        P_Left->countL = 0;
        ras.Proc_Sweep_Drop( RAS_VARS  y,
                                       P_Left->X,
                                       P_Right->X,
                                       P_Left,
                                       P_Right );
      }

      P_Left  = P_Left->link;
      P_Right = P_Right->link;
    }

    goto Next_Line;
  }


/****************************************************************************/
/*                                                                          */
/* Function:    Render_Single_Pass                                          */
/*                                                                          */
/* Description: Performs one sweep with sub-banding.                        */
/*                                                                          */
/* Input:       _XCoord, _YCoord : x and y coordinates arrays               */
/*                                                                          */
/* Returns:     SUCCESS on success                                          */
/*              FAILURE if any error was encountered during render.         */
/*                                                                          */
/****************************************************************************/

  static TT_Error  Render_Single_Pass( RAS_ARGS Bool  flipped )
  {
    Short  i, j, k;
    Int    band_top = 0;


    while ( band_top >= 0 )
    {
      ras.maxY = (Long)ras.band_stack[band_top].y_max * ras.precision;
      ras.minY = (Long)ras.band_stack[band_top].y_min * ras.precision;

      ras.top = MemDeref( ras.buffer );

      ras.error = Raster_Err_None;

      if ( Convert_Glyph( RAS_VARS  flipped ) )
      {
        if ( ras.error != Raster_Err_Overflow ) return FAILURE;

        ras.error = Raster_Err_None;

        /* sub-banding */

        i = ras.band_stack[band_top].y_min;
        j = ras.band_stack[band_top].y_max;

        k = ( i + j ) >> 1;

        if ( band_top >= 7 || k < i )
        {
          band_top     = 0;
          return Raster_Err_Invalid;
        }

        ras.band_stack[band_top+1].y_min = k;
        ras.band_stack[band_top+1].y_max = j;
        ras.band_stack[band_top].y_max = k - 1;

        ++band_top;
      }
      else
      {
        if ( ras.fProfile )
        {
          if ( Draw_Sweep( RAS_VAR ) ) return ras.error;
        }
        else
          ras.Proc_Sweep_Init( RAS_VAR, 0 );

        --band_top;
      }
    }

    return TT_Err_Ok;
  }


/****************************************************************************/
/*                                                                          */
/* Function:    Render_Bitmap_Glyph                                         */
/*                                                                          */
/* Description: Renders a glyph in a bitmap.  Sub-banding if needed.        */
/*                                                                          */
/* Input:       AGlyph   Glyph record                                       */
/*                                                                          */
/* Returns:     SUCCESS on success.                                         */
/*              FAILURE if any error was encountered during rendering.      */
/*                                                                          */
/****************************************************************************/

  LOCAL_FUNC
  TT_Error  Render_Bitmap_Glyph( RAS_ARGS TT_Outline*     glyph,
                                          TT_Raster_Map*  target_map )
  {
    TT_Error  error;


EC( ECCheckMemHandle( ras.buffer ) );

    if ( glyph->n_points == 0 || glyph->n_contours <= 0 )
      return TT_Err_Ok;

    if ( glyph->n_points < glyph->contours[glyph->n_contours - 1] )
      return TT_Err_Too_Many_Points;

    if ( target_map )
      ras.target = *target_map;

    ras.outs      = glyph->contours;
    ras.flags     = glyph->flags;
    ras.nPoints   = glyph->n_points;
    ras.nContours = glyph->n_contours;
    ras.coords    = glyph->points;

    Set_Resolution( RAS_VARS glyph->y_ppem );
    ras.dropOutControl = glyph->dropout_mode;

    /* Vertical Sweep */
    ras.Proc_Sweep_Init   = Vertical_Sweep_Init;
    ras.Proc_Sweep_Span   = Vertical_Sweep_Span;
    ras.Proc_Sweep_Drop   = Vertical_Sweep_Drop;
    ras.Proc_Sweep_Step   = Vertical_Sweep_Step;

    ras.band_stack[0].y_min = 0;
    ras.band_stack[0].y_max = ras.target.rows - 1;

    ras.bWidth  = ras.target.width;
    ras.bTarget = (Byte*)ras.target.bitmap;

    /* lock renderpool cache */
    LOCK_RENDER_POOL;

    if ( (error = Render_Single_Pass( RAS_VARS 0 )) != 0 )
      goto Fin;

    /* Horizontal Sweep */

    if ( glyph->second_pass && ras.dropOutControl != 0 )
    {
      ras.Proc_Sweep_Init   = Horizontal_Sweep_Init;
      ras.Proc_Sweep_Span   = Horizontal_Sweep_Span;
      ras.Proc_Sweep_Drop   = Horizontal_Sweep_Drop;
      ras.Proc_Sweep_Step   = Horizontal_Sweep_Step;

      ras.band_stack[0].y_min = 0;
      ras.band_stack[0].y_max = ras.target.width - 1;

      error = Render_Single_Pass( RAS_VARS  1 );
    }

  Fin:
    UNLOCK_RENDER_POOL;
    return error;
  }


#ifdef __GEOS__

/****************************************************************************/
/*                                                                          */
/* Function:    Render_Region_Glyph                                         */
/*                                                                          */
/* Description: Renders a glyph in a region.  Sub-banding if needed.        */
/*                                                                          */
/* Input:       AGlyph   Glyph record                                       */
/*                                                                          */
/* Returns:     SUCCESS on success.                                         */
/*              FAILURE if any error was encountered during rendering.      */
/*                                                                          */
/****************************************************************************/

  LOCAL_FUNC
  TT_Error  Render_Region_Glyph( RAS_ARGS TT_Outline*     glyph,
                                          TT_Raster_Map*  map )
  {
    TT_Error  error;


EC( ECCheckMemHandle( ras.buffer ) );

    if ( glyph->n_points == 0 || glyph->n_contours <= 0 )
      return TT_Err_Ok;

    if ( glyph->n_points < glyph->contours[glyph->n_contours - 1] )
      return TT_Err_Too_Many_Points;

    if ( map )
      ras.target = *map;

    ras.outs      = glyph->contours;
    ras.flags     = glyph->flags;
    ras.nPoints   = glyph->n_points;
    ras.nContours = glyph->n_contours;
    ras.coords    = glyph->points;

    Set_Resolution( RAS_VARS glyph->y_ppem );
    ras.dropOutControl = glyph->dropout_mode;

    /* Vertical Sweep */
  
    ras.Proc_Sweep_Init   = Vertical_Region_Sweep_Init;
    ras.Proc_Sweep_Span   = Vertical_Region_Sweep_Span;
    ras.Proc_Sweep_Drop   = Vertical_Region_Sweep_Drop;
    ras.Proc_Sweep_Step   = Vertical_Region_Sweep_Step;

    ras.band_stack[0].y_min = 0;
    ras.band_stack[0].y_max = ras.target.rows - 1;

    ras.bWidth  = ras.target.cols;
    ras.bTarget = (PByte)ras.target.bitmap;


    /* lock renderpool cache */
    LOCK_RENDER_POOL;

    if ( (error = Render_Single_Pass( RAS_VARS 0 )) != 0 )
      goto Fin;

    Region_Sweep_Finish( RAS_VAR );
    map->size = ras.target.size;

  Fin:
    UNLOCK_RENDER_POOL;
    return error;
  }

#endif  /* __GEOS__ */

#define  RASTER_RENDER_POOL_SAFETY    256
static void Lock_Render_Pool( RAS_ARGS  TT_Outline*  glyph )
{
  /* estimated size of the renderpool */
  TT_UShort   renderpoolSize = ( glyph->y_ppem >> 3 ) * RASTER_RENDER_POOL_FACTOR 
                                                      + RASTER_RENDER_POOL_MIN_SIZE;

  //if( MemGetInfo( ras.buffer, MGIT_SIZE ) != renderpoolSize )
    MemReAlloc( ras.buffer, renderpoolSize, HAF_NO_ERR | HAF_LOCK );

  ras.sizeBuff = (PStorage)MemDeref( ras.buffer ) + ( renderpoolSize / sizeof(long) );

  EC_ERROR_IF(ras.sizeBuff == 0, -1);
}


/************************************************/
/*                                              */
/* InitRasterizer                               */
/*                                              */
/*  Raster Initialization.                      */
/*  Gets the bitmap description and render pool */
/*  addresses.                                  */
/*                                              */
/************************************************/

#undef ras

  LOCAL_FUNC
  TT_Error  TTRaster_Done( )
  {
    TRaster_Instance*  ras = (TRaster_Instance*)engineInstance.raster_component;


    if ( !ras )
      return TT_Err_Ok;

    GEO_FREE( ras->buffer);

#ifndef TT_CONFIG_OPTION_STATIC_RASTER
    FREE( ras );
#endif

    return TT_Err_Ok;
  }


  LOCAL_FUNC
  TT_Error  TTRaster_Init( )
  {
    TT_Error           error;
    TRaster_Instance*  ras;


#ifdef TT_CONFIG_OPTION_STATIC_RASTER
    ras = engineInstance.raster_component = &cur_ras;
#else
    if ( ALLOC( engineInstance.raster_component, sizeof ( TRaster_Instance ) ) )
      return error;

    ras = (TRaster_Instance*)engineInstance.raster_component;
#endif

    ras->buffer = MemAllocSetOwner( GeodeGetCodeProcessHandle(), 
                      RASTER_RENDER_POOL_INITIAL,
                      HF_DISCARDABLE | HF_DISCARDED | HF_SHARABLE | HF_SWAPABLE, 
                      HAF_NO_ERR );

    ras->error  = Raster_Err_None;

    return TT_Err_Ok;
  }


/* END */
