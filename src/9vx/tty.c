/*
 * Terminal support (standard input/output).
 */

#include	"u.h"
#include	<termios.h>
#include	<sys/termios.h>
#include	"lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"error.h"

static int ttyprint = 0;
static int ttyecho = 0;
static struct termios ttprevmode;

/*
 * Normal prints and console output go to standard output.
 */
void
uartputs(char *buf, int n)
{
	if(!ttyprint)
		return;
	write(1, buf, n);
}

void
restoretty(void)
{
	if(ttyecho && tcsetattr(0, TCSANOW, &ttprevmode) < 0){
		ttyecho = 0;
		panic("could not restore previous tty mode");
	}
}

void
bye(int sig)
{
	restoretty();
	exit(0);
}

void
uartreader(void *v)
{
	char buf[256];
	int n;
	static struct termios ttmode;
	
	/*
	 * Try to disable host echo, save
	 * current state to restore it at exit. 
	 * If successful, remember to echo
	 * what gets typed ourselves.
	 */
	if(tcgetattr(0, &ttprevmode) < 0)
		panic("could not read tty current mode");
	if(tcgetattr(0, &ttmode) >= 0){
		ttmode.c_lflag &= ~(ECHO|ICANON);
		if(tcsetattr(0, TCSANOW, &ttmode) >= 0)
			ttyecho = 1;
	}
	signal(SIGINT, bye);
	signal(SIGTERM, bye);
	while((n = read(0, buf, sizeof buf)) > 0)
		echo(buf, n);
}

void
uartinit(int usetty)
{
	ttyprint = usetty;
	kbdq = qopen(4*1024, 0, 0, 0);
	if(usetty)
		kproc("*tty*", uartreader, nil);
}

void
uartecho(char *buf, int n)
{
	if(ttyecho)
		write(1, buf, n);
}

