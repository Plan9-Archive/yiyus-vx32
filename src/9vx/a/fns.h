#include "portfns.h"

void	aamloop(int);
Dirtab*	addarchfile(char*, int, long(*)(Chan*,void*,long,vlong), long(*)(Chan*,void*,long,vlong));
void	archinit(void);
void	bootargs(void*);
ulong	cankaddr(ulong);
void	clockintr(Ureg*, void*);
int	(*cmpswap)(long*, long, long);
int	cmpswap486(long*, long, long);
void	(*coherence)(void);
void	cpuid(char*, int*, int*);
int	cpuidentify(void);
void	cpuidprint(void);
void	(*cycles)(uvlong*);
void	delay(int);
int	dmacount(int);
int	dmadone(int);
void	dmaend(int);
int	dmainit(int, int);
long	dmasetup(int, void*, long, int);
#define	evenaddr(x)				/* x86 doesn't care */
void	fpclear(void);
void	fpenv(FPsave*);
void	fpinit(void);
void	fpoff(void);
void	fprestore(FPsave*);
void	fpsave(FPsave*);
ulong	fpstatus(void);
ulong	getcr0(void);
ulong	getcr2(void);
ulong	getcr3(void);
ulong	getcr4(void);
char*	getconf(char*);
void	guesscpuhz(int);
void	halt(void);
int	i8042auxcmd(int);
int	i8042auxcmds(uchar*, int);
void	i8042auxenable(void (*)(int, int));
void	i8042reset(void);
void	i8250console(void);
void*	i8250alloc(int, int, int);
void	i8250mouse(char*, int (*)(Queue*, int), int);
void	i8250setmouseputc(char*, int (*)(Queue*, int));
void	i8253enable(void);
void	i8253init(void);
void	i8253link(void);
uvlong	i8253read(uvlong*);
void	i8253timerset(uvlong);
int	i8259disable(int);
int	i8259enable(Vctl*);
void	i8259init(void);
int	i8259isr(int);
void	i8259on(void);
void	i8259off(void);
int	i8259vecno(int);
void	idle(void);
void	idlehands(void);
int	inb(int);
void	insb(int, void*, int);
ushort	ins(int);
void	inss(int, void*, int);
ulong	inl(int);
void	insl(int, void*, int);
int	intrdisable(int, void (*)(Ureg *, void *), void*, int, char*);
void	intrenable(int, void (*)(Ureg*, void*), void*, int, char*);
void	introff(void);
void	intron(void);
void	invlpg(ulong);
void	iofree(int);
void	ioinit(void);
int	iounused(int, int);
int	ioalloc(int, int, int, char*);
int	ioreserve(int, int, int, char*);
int	iprint(char*, ...);
int	isaconfig(char*, int, ISAConf*);
void*	kaddr(ulong);
void	kbdenable(void);
void	kbdinit(void);
#define	kmapinval()
void	lgdt(ushort[3]);
void	lidt(ushort[3]);
void	links(void);
void	ltr(ulong);
void	mach0init(void);
void	mathinit(void);
void	mb386(void);
void	mb586(void);
void	meminit(void);
void	memorysummary(void);
#define mmuflushtlb(pdb) putcr3(pdb)
void	mmuinit(void);
ulong*	mmuwalk(ulong*, ulong, int, int);
uchar	nvramread(int);
void	nvramwrite(int, uchar);
void	outb(int, int);
void	outsb(int, void*, int);
void	outs(int, ushort);
void	outss(int, void*, int);
void	outl(int, ulong);
void	outsl(int, void*, int);
ulong	paddr(void*);
void	pcireset(void);
void	pcmcisread(PCMslot*);
int	pcmcistuple(int, int, int, void*, int);
PCMmap*	pcmmap(int, ulong, int, int);
int	pcmspecial(char*, ISAConf*);
int	(*_pcmspecial)(char *, ISAConf *);
void	pcmspecialclose(int);
void	(*_pcmspecialclose)(int);
void	pcmunmap(int, PCMmap*);
int	pdbmap(ulong*, ulong, ulong, int);
void	procrestore(Proc*);
void	procsave(Proc*);
void	procsetup(Proc*);
void	putcr3(ulong);
void	putcr4(ulong);
void*	rampage(void);
void	rdmsr(int, vlong*);
void	realmode(Ureg*);
void	screeninit(void);
void	(*screenputs)(char*, int);
void	syncclock(void);
void*	tmpmap(Page*);
void	tmpunmap(void*);
void	touser(void*);
void	trapenable(int, void (*)(Ureg*, void*), void*, char*);
void	trapinit(void);
void	trapinit0(void);
int	tas(void*);
uvlong	tscticks(uvlong*);
ulong	umbmalloc(ulong, int, int);
void	umbfree(ulong, int);
ulong	umbrwmalloc(ulong, int, int);
void	umbrwfree(ulong, int);
ulong	upaalloc(int, int);
void	upafree(ulong, int);
void	upareserve(ulong, int);
#define	userureg(ur) (((ur)->cs & 0xFFFF) == UESEL)
void	vectortable(void);
void*	vmap(ulong, int);
int	vmapsync(ulong);
void	vunmap(void*, int);
void	wrmsr(int, vlong);
int	xchgw(ushort*, int);

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))
#define	KADDR(a)	kaddr(a)
#define PADDR(a)	paddr((void*)(a))

#define	dcflush(a, b)

// Plan 9 VX additions
void	gotolabel(Label*);
void	labelinit(Label *l, ulong pc, ulong sp);
void	latin1putc(int, void(*)(int));
void	makekprocdev(Dev*);
void	newmach(void);
void	oserror(void);
void	oserrstr(void);
int	setlabel(Label*);
int	tailkmesg(char*, int);
void	trap(Ureg*);
void	uartecho(char*, int);
void	uartinit(int);
void	*uvalidaddr(ulong addr, ulong len, int write);
int	isuaddr(void*);
void	setsigsegv(int invx32);

#define GSHORT(p)	(((p)[1]<<8)|(p)[0])
#define GLONG(p)	((GSHORT(p+2)<<16)|GSHORT(p))

void	plock(Psleep*);
void	punlock(Psleep*);
void	pwakeup(Psleep*);
void	psleep(Psleep*);

