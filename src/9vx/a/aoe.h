enum {
	ACata,
	ACconfig,
};

enum {
	AQCread,
	AQCtest,
	AQCprefix,
	AQCset,
	AQCfset,
};

enum {
	AEcmd	= 1,
	AEarg,
	AEdev,
	AEcfg,
	AEver,
};

enum {
	Aoetype	= 0x88a2,
	Aoesectsz = 512,
	Szaoeata	= 24+12,
	Szaoeqc	= 24+8,
	Aoever	= 1,

	AFerr	= 1<<2,
	AFrsp	= 1<<3,

	AAFwrite= 1,
	AAFext	= 1<<6,
};

typedef struct {
	uchar	dst[Eaddrlen];
	uchar	src[Eaddrlen];
	uchar	type[2];
	uchar	verflag;
	uchar	error;
	uchar	major[2];
	uchar	minor;
	uchar	cmd;
	uchar	tag[4];
} Aoehdr;

typedef struct {
	uchar	dst[Eaddrlen];
	uchar	src[Eaddrlen];
	uchar	type[2];
	uchar	verflag;
	uchar	error;
	uchar	major[2];
	uchar	minor;
	uchar	cmd;
	uchar	tag[4];
	uchar	aflag;
	uchar	errfeat;
	uchar	scnt;
	uchar	cmdstat;
	uchar	lba[6];
	uchar	res[2];
} Aoeata;

typedef struct {
	uchar	dst[Eaddrlen];
	uchar	src[Eaddrlen];
	uchar	type[2];
	uchar	verflag;
	uchar	error;
	uchar	major[2];
	uchar	minor;
	uchar	cmd;
	uchar	tag[4];
	uchar	bufcnt[2];
	uchar	fwver[2];
	uchar	scnt;
	uchar	verccmd;
	uchar	cslen[2];
} Aoeqc;

extern char Echange[];
extern char Enotup[];
