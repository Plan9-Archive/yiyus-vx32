#define	BOOTLINELEN	64
#define	BOOTARGSLEN	(3584-0x200-BOOTLINELEN)
#define	MAXCONF		100

char	inibuf[BOOTARGSLEN];
char	*iniline[MAXCONF];
int	cpulimit;	/* max cpu usage */
int	initrc;	/* run rc instead of init */
int	nofork;	/* do not fork at init */
int	nogui;	/* do not start the gui */
int	usetty;	/* use tty for input/output */
int	memsize;	/* memory size */
int	bootargc;
char**	bootargv;
char*	inifile;
char*	localroot;
char*	username;

int	readini(char *fn);
void	inifields(void (*fp)(char*, char*));
void	iniopt(char*, char*);
void	inienv(char*, char*);
void	printconfig(char*);
