##############################################################################
#
# 	Copyright (c) GeoWorks 1988 -- All Rights Reserved
#
# PROJECT:	PC GEOS
# MODULE:	Swat System Library -- autoloaded function definitions
# FILE: 	autoload.tcl
# AUTHOR: 	Adam de Boor, May  5, 1989
#
# COMMANDS:
# 	Name			Description
#	----			-----------
#
# REVISION HISTORY:
#	Name	Date		Description
#	----	----		-----------
#	ardeb	5/ 5/89		Initial Revision
#
# DESCRIPTION:
#	Things to be autoloaded
#
#	$Id: autoload.tcl,v 3.120 97/05/23 08:47:50 weber Exp $
#
###############################################################################
[autoload apropos 1 help]
[autoload ascii 0 ref]
[autoload assign 0 assign]
[autoload bin 1 bits]
[autoload bits 1 bits]
[autoload call 1 call]
[autoload call-patient 1 call]
[autoload cycles 1 timing]
[autoload dbrk 1 dbrk]
[autoload dcall 1 Extra/dcall]
[autoload ecbrk 1 ibrk]
[autoload emacs 0 Internal/emacs]
[autoload ewatch 0 Internal/emacs]
[autoload exit 1 call]
[autoload flagwin 1 flags]
[autoload fmtoptr 1 print]
[autoload fmtval 1 print]
[autoload gloss 1 ref]
[autoload hbrk 1 hbrk]
[autoload help 0 help]
[autoload synopsis 0 help]
[autoload inst 0 ref]
[autoload int 1 int]
[autoload intr 1 int]
[autoload ls 1 Internal/unix]
[autoload patch 1 patch]
[autoload patchin 1 patch]
[autoload patchout 1 patch]
[autoload pbitmap 1 print]
[autoload pdw 0 print]
[autoload pflags 1 flags]
[autoload pmake 1 Internal/unix]
[autoload precord 1 print]
[autoload penum 1 print]
[autoload print 2 print]
[autoload _print 1 print]
[autoload print-cur-regs 1 curregs]
[autoload ps 1 ps]
[autoload pscope 1 whatis]
[autoload ref 1 ref]
[autoload getcc 1 setcc]
[autoload setcc 1 setcc]
[autoload clrcc 1 setcc]
[autoload compcc 1 setcc]

[autoload showcalls 1 showcall]

[autoload slist 0 srclist]
[autoload smatch 1 smatch]
[autoload spawn 0 process]
[autoload tbrk 0 tbrk]
[autoload timebrk 1 timebrk]
[autoload vi 1 Internal/unix]
[autoload vif 1 Internal/unix]
[autoload which 1 Internal/unix]
[autoload wakeup 0 process]
[autoload wakeup-thread 1 process]
[autoload whatat 1 whatat]
[autoload whatis 1 whatis]
[autoload why 1 fatalerr]
[autoload why-warning 1 fatalerr]
[autoload explain 1 fatalerr]
[autoload xref 1 ref]

##############################################################################
#                                                                            #
#   	    	    	    KERNEL PLAYTHINGS				     #
#									     #
##############################################################################
[autoload brkload 1 brkload]
[autoload bwatch 1 borrow]
[autoload carray-enum 1 chunkarr]
[autoload dbwatch 0 Extra/dbwatch]
[autoload diskwalk 1 fs]
[autoload drivewalk 1 fs]
[autoload ec 0 ec]
[autoload elist 1 event]
[autoload ensure-vm-block-resident 1 vm]
[autoload erfind 1 event]
[autoload eqfind 1 event]
[autoload eqlist 1 event]
[autoload fhandle 0 fs]
[autoload fhan 1 heap]
[autoload freeze 1 thread]
[autoload fsdwalk 1 fs]
[autoload fsir-stat 1 fs]
[autoload fwalk 1 fs]
[autoload geosfiles 1 dos]
[autoload handles 1 heap]
[autoload handsum 1 heap]
[autoload hgwalk 1 heap]
[autoload heapspace 1 heap]
[autoload hname 1 heap]
[autoload hwalk 1 heap]
[autoload iniwatch 1 pini]
[autoload isclassptr 1 object]
[autoload kbdperf 1 Internal/uiperf]
[autoload lhwalk 1 lm]
[autoload loadapp 1 Extra/gload]
[autoload loadgeode 1 Extra/gload]
[autoload map-method 1 object]
[autoload memsize 1 heap]
[autoload omfq 1 object]
[autoload pevent 1 event]
[autoload phandle 0 heap]
[autoload psegment 0 heap]
[autoload pini 1 pini]
[autoload pdb 1 vm]
[autoload print-db-group 1 db]
[autoload print-db-item 1 db]
[autoload pdisk 1 fs]
[autoload pdrive 1 fs]
[autoload pgcnblock 1 gcn]
[autoload pgcnlist 1 gcn]
[autoload pharray 1 hugearr]
[autoload preg 1 region]
[autoload pthread 0 thread]
[autoload ptimer 1 timer]
[autoload ptr 1 event]
[autoload pvmb 1 vm]
[autoload pvmt 1 vm]
[autoload sbwalk 0 user]
[autoload set-startup-ec 1 ec]
[autoload sysfiles 1 dos]
[autoload thaw 1 thread]
[autoload block 1 thread]
[autoload unblock 1 thread]
[autoload threadname 1 print]
[autoload threadstat 1 thread]
[autoload timer-profile 1 Extra/timeprof]
[autoload tmem 1 heap]
[autoload twalk 0 timer]
[autoload waitpostinfo 0 dos]
[autoload wintree 0 wintree]

##############################################################################
#                                                                            #
#   	    	    	    UI PLAYTHINGS				     #
#									     #
##############################################################################
#[autoload fieldwin 1 user]
#[autoload obj-prof 1 objprof]
#[autoload prspec 0 user]
#[autoload screenwin 1 user]
[autoload addr-with-obj-flag 1 user]
[autoload appobj 1 user]
[autoload classes 1 class]
[autoload content 1 user]
[autoload cup 1 class]
[autoload fetch-optr 1 object]
[autoload find-master 1 object]
[autoload flowobj 1 user]
[autoload focus 1 grab]
[autoload focusobj 1 grab]
[autoload fvardata 1 pvardata]
[autoload gentree 1 objtree]
[autoload get-chunk-addr-from-obj-addr 1 object]
[autoload gup 1 objtree]
[autoload iacp 1 iacp]
[autoload impgentree 1 objtree]
[autoload impliedgrab 1 user]
[autoload impliedwin 1 user]
[autoload impvistree 1 objtree]
[autoload is-master 1 object]
[autoload is-obj-in-class 1 grab]
[autoload keyboard 1 grab]
[autoload keyboardobj 1 grab]
[autoload long-class 1 object]
[autoload methods 1 class]
[autoload model 1 grab]
[autoload modelobj 1 grab]
[autoload mouse 1 grab]
[autoload mouseobj 1 grab]
[autoload mwatch 1 objwatch]
[autoload next-master 1 object]
[autoload obj-class 1 object]
[autoload obj-find-method 1 object]
[autoload obj-foreach-class 1 object]
[autoload obj-name 1 object]
[autoload objbrk 0 objbrk]
[autoload objtree 1 objtree]
[autoload objwalk 1 lm]
[autoload objwatch 0 objwatch]
[autoload pappcache 1 appcache]
[autoload pcarray 1 chunkarr]
[autoload pclass 0 objtree]
[autoload pgen 0 user]
[autoload pgs 1 pvm]
[autoload phint 1 phint]
[autoload pinst 1 pobject]
[autoload piv 1 pobject]
[autoload plenstring 1 pvm]
[autoload pnormal 1 clipbrd]
[autoload pobject 0 pobject]
[autoload pobjgcnlist 1 gcn]
[autoload pobjmon 1 pvm]
[autoload pod 1 objwatch]
[autoload ppath 1 pvm]
[autoload pquick 1 clipbrd]
[autoload print-clipboard-item 1 clipbrd]
[autoload print-method-table 1 object]
[autoload print-obj-and-method 1 object]
[autoload procmessagebrk 1 objwatch]
[autoload pstring 1 pvm]
[autoload psup 1 grab]
[autoload pvardata 1 pvardata]
[autoload pvardentry 1 pvardata]
[autoload pvardrange 1 pvardata]
[autoload pvis 0 user]
[autoload pvismon 1 pvm]
[autoload pvsize 1 user]
[autoload run 1 call]
[autoload step-to 1 istep]
[autoload step-while 1 istep]
[autoload systemobj 1 user]
[autoload target 1 grab]
[autoload targetobj 1 grab]
[autoload vistree 1 objtree]
[autoload vup 1 objtree]

##############################################################################
#                                                                            #
#   	    	    	    OTHER PLAYTHINGS				     #
#									     #
##############################################################################
[autoload fonts 1 Extra/font]
[autoload getstring 1 cwd]
[autoload showfonts 1 Extra/font]
[autoload pluralize 1 putils]
[autoload psize 1 putils]
[autoload ptext 1 ptext]
[autoload tundocalls 1 ptext]
[autoload size 1 putils]
[autoload videolog 1 Internal/video]
[autoload pwd 1 cwd]
[autoload dirs 1 cwd]
[autoload stdpaths 1 cwd]
[autoload pfloat 1 fp]
[autoload fpstack 1 fp]
[autoload rmod 1 Extra/analysis]
[autoload analbrk 1 Extra/analysis]
[autoload workset 1 Extra/analysis]
[autoload restally 1 Extra/analysis]
[autoload rmsg 1 Extra/analysis]
[autoload int21 1 Extra/analysis]
[autoload pssheet 1 pssheet]
[autoload pcelldata 1 pssheet]
[autoload pcelldeps 1 pssheet]
[autoload pdgroup 1 print]

[autoload grobjtree 1 grobj]
[autoload pbody 1 grobj]
[autoload ptrans 1 grobj]
[autoload pobjarray 1 grobj]

[autoload fpustate 1 fp]

[autoload netlib 0 Internal/netlib]

[autoload doc 1 doc.tcl]
[autoload view 1 srclist]
[autoload send 1 filexfer]
[autoload run 1 call]
[autoload patient-default 1 patient]
[load pmode]
[autoload hwbrk 1 hwbrk]
[autoload ignerr 0 ignerr]

