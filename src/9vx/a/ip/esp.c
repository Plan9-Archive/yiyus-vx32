/*
 * Encapsulating Security Payload for IPsec for IPv4, rfc1827.
 *	currently only implements tunnel mode.
 * TODO: update to match rfc4303.
 */
#include	"u.h"
#include	"lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"error.h"

#include	"ip.h"
#include	"ipv6.h"
#include	"libsec.h"

typedef struct Esphdr Esphdr;
typedef struct Esp4hdr Esp4hdr;
typedef struct Esp6hdr Esp6hdr;
typedef struct Esptail Esptail;
typedef struct Userhdr Userhdr;
typedef struct Esppriv Esppriv;
typedef struct Espcb Espcb;
typedef struct Algorithm Algorithm;

enum
{
	IP_ESPPROTO	= 50,	/* IP v4 and v6 protocol number */
	Esp4hdrlen	= IP4HDR + 8,
	Esp6hdrlen	= IP6HDR + 8,

	Esptaillen	= 2,	/* does not include pad or auth data */
	Userhdrlen	= 4,	/* user-visible header size - if enabled */
};

struct Esphdr
{
	uchar	espspi[4];	/* Security parameter index */
	uchar	espseq[4];	/* Sequence number */
};

/*
 * tunnel-mode layout:		IP | ESP | TCP/UDP | user data.
 * transport-mode layout is:	ESP | IP | TCP/UDP | user data.
 */
struct Esp4hdr
{
	/* ipv4 header */
	uchar	vihl;		/* Version and header length */
	uchar	tos;		/* Type of service */
	uchar	length[2];	/* packet length */
	uchar	id[2];		/* Identification */
	uchar	frag[2];	/* Fragment information */
	uchar	Unused;
	uchar	espproto;	/* Protocol */
	uchar	espplen[2];	/* Header plus data length */
	uchar	espsrc[4];	/* Ip source */
	uchar	espdst[4];	/* Ip destination */

	/* Esphdr; */
	uchar	espspi[4];	/* Security parameter index */
	uchar	espseq[4];	/* Sequence number */
};

/* tunnel-mode layout */
struct Esp6hdr
{
	/* Ip6hdr; */
	uchar	vcf[4];		/* version:4, traffic class:8, flow label:20 */
	uchar	ploadlen[2];	/* payload length: packet length - 40 */
	uchar	proto;		/* next header type */
	uchar	ttl;		/* hop limit */
	uchar	src[IPaddrlen];
	uchar	dst[IPaddrlen];

	/* Esphdr; */
	uchar	espspi[4];	/* Security parameter index */
	uchar	espseq[4];	/* Sequence number */
};

struct Esptail
{
	uchar	pad;
	uchar	nexthdr;
};

/* header as seen by the user */
struct Userhdr
{
	uchar	nexthdr;	/* next protocol */
	uchar	unused[3];
};

struct Esppriv
{
	ulong	in;
	ulong	inerrors;
};

/*
 *  protocol specific part of Conv
 */
struct Espcb
{
	int	incoming;
	int	header;		/* user user level header */
	ulong	spi;
	ulong	seq;		/* last seq sent */
	ulong	window;		/* for replay attacks */
	char	*espalg;
	void	*espstate;	/* other state for esp */
	int	espivlen;	/* in bytes */
	int	espblklen;
	int	(*cipher)(Espcb*, uchar *buf, int len);
	char	*ahalg;
	void	*ahstate;	/* other state for esp */
	int	ahlen;		/* auth data length in bytes */
	int	ahblklen;
	int	(*auth)(Espcb*, uchar *buf, int len, uchar *hash);
};

struct Algorithm
{
	char 	*name;
	int	keylen;		/* in bits */
	void	(*init)(Espcb*, char* name, uchar *key, int keylen);
};

static	Conv* convlookup(Proto *esp, ulong spi);
static	char *setalg(Espcb *ecb, char **f, int n, Algorithm *alg);
static	void espkick(void *x);

static	void nullespinit(Espcb*, char*, uchar *key, int keylen);
static	void desespinit(Espcb *ecb, char *name, uchar *k, int n);

static	void nullahinit(Espcb*, char*, uchar *key, int keylen);
static	void shaahinit(Espcb*, char*, uchar *key, int keylen);
static	void md5ahinit(Espcb*, char*, uchar *key, int keylen);

static Algorithm espalg[] =
{
	"null",			0,	nullespinit,
//	"des3_cbc",		192,	des3espinit,	/* rfc2451 */
//	"aes_128_cbc",		128,	aescbcespinit,	/* rfc3602 */
//	"aes_ctr",		128,	aesctrespinit,	/* rfc3686 */
	"des_56_cbc",		64,	desespinit,	/* rfc2405, deprecated */
//	"rc4_128",		128,	rc4espinit,	/* gone in rfc4305 */
	nil,			0,	nil,
};

static Algorithm ahalg[] =
{
	"null",			0,	nullahinit,
	"hmac_sha1_96",		128,	shaahinit,	/* rfc2404 */
//	"aes_xcbc_mac_96",	128,	aesahinit,	/* rfc3566 */
	"hmac_md5_96",		128,	md5ahinit,	/* rfc2403 */
	nil,			0,	nil,
};

static char*
espconnect(Conv *c, char **argv, int argc)
{
	char *p, *pp;
	char *e = nil;
	ulong spi;
	Espcb *ecb = (Espcb*)c->ptcl;

	switch(argc) {
	default:
		e = "bad args to connect";
		break;
	case 2:
		p = strchr(argv[1], '!');
		if(p == nil){
			e = "malformed address";
			break;
		}
		*p++ = 0;
		parseip(c->raddr, argv[1]);
		findlocalip(c->p->f, c->laddr, c->raddr);
		ecb->incoming = 0;
		ecb->seq = 0;
		if(strcmp(p, "*") == 0) {
			QLOCK(c->p);
			for(;;) {
				spi = nrand(1<<16) + 256;
				if(convlookup(c->p, spi) == nil)
					break;
			}
			QUNLOCK(c->p);
			ecb->spi = spi;
			ecb->incoming = 1;
			qhangup(c->wq, nil);
		} else {
			spi = strtoul(p, &pp, 10);
			if(pp == p) {
				e = "malformed address";
				break;
			}
			ecb->spi = spi;
			qhangup(c->rq, nil);
		}
		nullespinit(ecb, "null", nil, 0);
		nullahinit(ecb, "null", nil, 0);
	}
	Fsconnected(c, e);

	return e;
}


static int
espstate(Conv *c, char *state, int n)
{
	return snprint(state, n, "%s", c->inuse?"Open\n":"Closed\n");
}

static void
espcreate(Conv *c)
{
	c->rq = qopen(64*1024, Qmsg, 0, 0);
	c->wq = qopen(64*1024, Qkick, espkick, c);
}

static void
espclose(Conv *c)
{
	Espcb *ecb;

	qclose(c->rq);
	qclose(c->wq);
	qclose(c->eq);
	ipmove(c->laddr, IPnoaddr);
	ipmove(c->raddr, IPnoaddr);

	ecb = (Espcb*)c->ptcl;
	free(ecb->espstate);
	free(ecb->ahstate);
	memset(ecb, 0, sizeof(Espcb));
}

static int
ipvers(Conv *c)
{
	if((memcmp(c->raddr, v4prefix, IPv4off) == 0 &&
	    memcmp(c->laddr, v4prefix, IPv4off) == 0) ||
	    ipcmp(c->raddr, IPnoaddr) == 0)
		return V4;
	else
		return V6;
}

static void
espkick(void *x)
{
	Conv *c = x;
	Esp4hdr *eh4;
	Esp6hdr *eh6;
	Esptail *et;
	Userhdr *uh;
	Espcb *ecb;
	Block *bp;
	int nexthdr, payload, pad, align, version, hdrlen, iphdrlen;
	uchar *auth;

	version = ipvers(c);
	iphdrlen = version == V4? IP4HDR: IP6HDR;
	hdrlen =   version == V4? Esp4hdrlen: Esp6hdrlen;

	bp = qget(c->wq);
	if(bp == nil)
		return;

	QLOCK(c);
	ecb = c->ptcl;

	if(ecb->header) {
		/* make sure the message has a User header */
		bp = pullupblock(bp, Userhdrlen);
		if(bp == nil) {
			QUNLOCK(c);
			return;
		}
		uh = (Userhdr*)bp->rp;
		nexthdr = uh->nexthdr;
		bp->rp += Userhdrlen;
	} else {
		nexthdr = 0;	/* what should this be? */
	}

	payload = BLEN(bp) + ecb->espivlen;

	/* Make space to fit ip header */
	bp = padblock(bp, hdrlen + ecb->espivlen);

	align = 4;
	if(ecb->espblklen > align)
		align = ecb->espblklen;
	if(align % ecb->ahblklen != 0)
		panic("espkick: ahblklen is important after all");
	pad = (align-1) - (payload + Esptaillen-1)%align;

	/*
	 * Make space for tail
	 * this is done by calling padblock with a negative size
	 * Padblock does not change bp->wp!
	 */
	bp = padblock(bp, -(pad+Esptaillen+ecb->ahlen));
	bp->wp += pad+Esptaillen+ecb->ahlen;

	eh4 = (Esp4hdr *)bp->rp;
	eh6 = (Esp6hdr *)bp->rp;
	et = (Esptail*)(bp->rp + hdrlen + payload + pad);

	/* fill in tail */
	et->pad = pad;
	et->nexthdr = nexthdr;

	ecb->cipher(ecb, bp->rp + hdrlen, payload + pad + Esptaillen);
	auth = bp->rp + hdrlen + payload + pad + Esptaillen;

	/* fill in head */
	if (version == V4) {
		eh4->vihl = IP_VER4;
		hnputl(eh4->espspi, ecb->spi);
		hnputl(eh4->espseq, ++ecb->seq);
		v6tov4(eh4->espsrc, c->laddr);
		v6tov4(eh4->espdst, c->raddr);
		eh4->espproto = IP_ESPPROTO;
		eh4->frag[0] = 0;
		eh4->frag[1] = 0;
	} else {
		eh6->vcf[0] = IP_VER6;
		hnputl(eh6->espspi, ecb->spi);
		hnputl(eh6->espseq, ++ecb->seq);
		ipmove(eh6->src, c->laddr);
		ipmove(eh6->dst, c->raddr);
		eh6->proto = IP_ESPPROTO;
	}

	ecb->auth(ecb, bp->rp + iphdrlen, (hdrlen - iphdrlen) +
		payload + pad + Esptaillen, auth);

	QUNLOCK(c);
	/* print("esp: pass down: %uld\n", BLEN(bp)); */
	if (version == V4)
		ipoput4(c->p->f, bp, 0, c->ttl, c->tos, c);
	else
		ipoput6(c->p->f, bp, 0, c->ttl, c->tos, c);
}

void
espiput(Proto *esp, Ipifc* _, Block *bp)
{
	Esp4hdr *eh4;
	Esp6hdr *eh6;
	Esptail *et;
	Userhdr *uh;
	Conv *c;
	Espcb *ecb;
	uchar raddr[IPaddrlen], laddr[IPaddrlen];
	Fs *f;
	uchar *auth, *espspi;
	ulong spi;
	int payload, nexthdr, version, hdrlen;

	f = esp->f;
	if (bp == nil || BLEN(bp) == 0) {
		/* get enough to identify the IP version */
		bp = pullupblock(bp, IP4HDR);
		if(bp == nil) {
			netlog(f, Logesp, "esp: short packet\n");
			return;
		}
	}
	eh4 = (Esp4hdr*)bp->rp;
	version = ((eh4->vihl & 0xf0) == IP_VER4? V4: V6);
	hdrlen = version == V4? Esp4hdrlen: Esp6hdrlen;

	bp = pullupblock(bp, hdrlen + Esptaillen);
	if(bp == nil) {
		netlog(f, Logesp, "esp: short packet\n");
		return;
	}

	if (version == V4) {
		eh4 = (Esp4hdr*)bp->rp;
		spi = nhgetl(eh4->espspi);
		v4tov6(raddr, eh4->espsrc);
		v4tov6(laddr, eh4->espdst);
	} else {
		eh6 = (Esp6hdr*)bp->rp;
		spi = nhgetl(eh6->espspi);
		ipmove(raddr, eh6->src);
		ipmove(laddr, eh6->dst);
	}

	QLOCK(esp);
	/* Look for a conversation structure for this port */
	c = convlookup(esp, spi);
	if(c == nil) {
		QUNLOCK(esp);
		netlog(f, Logesp, "esp: no conv %I -> %I!%d\n", raddr,
			laddr, spi);
		icmpnoconv(f, bp);
		freeblist(bp);
		return;
	}

	QLOCK(c);
	QUNLOCK(esp);

	ecb = c->ptcl;
	/* too hard to do decryption/authentication on block lists */
	if(bp->next)
		bp = concatblock(bp);

	if(BLEN(bp) < hdrlen + ecb->espivlen + Esptaillen + ecb->ahlen) {
		QUNLOCK(c);
		netlog(f, Logesp, "esp: short block %I -> %I!%d\n", raddr,
			laddr, spi);
		freeb(bp);
		return;
	}

	auth = bp->wp - ecb->ahlen;
	espspi = version == V4? ((Esp4hdr*)bp->rp)->espspi:
				((Esp6hdr*)bp->rp)->espspi;
	if(!ecb->auth(ecb, espspi, auth - espspi, auth)) {
		QUNLOCK(c);
print("esp: bad auth %I -> %I!%ld\n", raddr, laddr, spi);
		netlog(f, Logesp, "esp: bad auth %I -> %I!%d\n", raddr,
			laddr, spi);
		freeb(bp);
		return;
	}

	payload = BLEN(bp) - hdrlen - ecb->ahlen;
	if(payload <= 0 || payload % 4 != 0 || payload % ecb->espblklen != 0) {
		QUNLOCK(c);
		netlog(f, Logesp, "esp: bad length %I -> %I!%d payload=%d BLEN=%d\n",
			raddr, laddr, spi, payload, BLEN(bp));
		freeb(bp);
		return;
	}
	if(!ecb->cipher(ecb, bp->rp + hdrlen, payload)) {
		QUNLOCK(c);
print("esp: cipher failed %I -> %I!%ld: %s\n", raddr, laddr, spi, up->errstr);
		netlog(f, Logesp, "esp: cipher failed %I -> %I!%d: %s\n", raddr,
			laddr, spi, up->errstr);
		freeb(bp);
		return;
	}

	payload -= Esptaillen;
	et = (Esptail*)(bp->rp + hdrlen + payload);
	payload -= et->pad + ecb->espivlen;
	nexthdr = et->nexthdr;
	if(payload <= 0) {
		QUNLOCK(c);
		netlog(f, Logesp, "esp: short packet after decrypt %I -> %I!%d\n",
			raddr, laddr, spi);
		freeb(bp);
		return;
	}

	/* trim packet */
	bp->rp += hdrlen + ecb->espivlen;
	bp->wp = bp->rp + payload;
	if(ecb->header) {
		/* assume Userhdrlen < Esp4hdrlen < Esp6hdrlen */
		bp->rp -= Userhdrlen;
		uh = (Userhdr*)bp->rp;
		memset(uh, 0, Userhdrlen);
		uh->nexthdr = nexthdr;
	}

	if(qfull(c->rq)){
		netlog(f, Logesp, "esp: qfull %I -> %I.%uld\n", raddr,
			laddr, spi);
		freeblist(bp);
	}else {
//		print("esp: pass up: %uld\n", BLEN(bp));
		qpass(c->rq, bp);
	}

	QUNLOCK(c);
}

char*
espctl(Conv *c, char **f, int n)
{
	Espcb *ecb = c->ptcl;
	char *e = nil;

	if(strcmp(f[0], "esp") == 0)
		e = setalg(ecb, f, n, espalg);
	else if(strcmp(f[0], "ah") == 0)
		e = setalg(ecb, f, n, ahalg);
	else if(strcmp(f[0], "header") == 0)
		ecb->header = 1;
	else if(strcmp(f[0], "noheader") == 0)
		ecb->header = 0;
	else
		e = "unknown control request";
	return e;
}

void
espadvise(Proto *esp, Block *bp, char *msg)
{
	Esp4hdr *h;
	Conv *c;
	ulong spi;

	h = (Esp4hdr*)(bp->rp);

	spi = nhgets(h->espspi);
	QLOCK(esp);
	c = convlookup(esp, spi);
	if(c != nil) {
		qhangup(c->rq, msg);
		qhangup(c->wq, msg);
	}
	QUNLOCK(esp);
	freeblist(bp);
}

int
espstats(Proto *esp, char *buf, int len)
{
	Esppriv *upriv;

	upriv = esp->priv;
	return snprint(buf, len, "%lud %lud\n",
		upriv->in,
		upriv->inerrors);
}

static int
esplocal(Conv *c, char *buf, int len)
{
	Espcb *ecb = c->ptcl;
	int n;

	QLOCK(c);
	if(ecb->incoming)
		n = snprint(buf, len, "%I!%uld\n", c->laddr, ecb->spi);
	else
		n = snprint(buf, len, "%I\n", c->laddr);
	QUNLOCK(c);
	return n;
}

static int
espremote(Conv *c, char *buf, int len)
{
	Espcb *ecb = c->ptcl;
	int n;

	QLOCK(c);
	if(ecb->incoming)
		n = snprint(buf, len, "%I\n", c->raddr);
	else
		n = snprint(buf, len, "%I!%uld\n", c->raddr, ecb->spi);
	QUNLOCK(c);
	return n;
}

static	Conv*
convlookup(Proto *esp, ulong spi)
{
	Conv *c, **p;
	Espcb *ecb;

	for(p=esp->conv; *p; p++){
		c = *p;
		ecb = c->ptcl;
		if(ecb->incoming && ecb->spi == spi)
			return c;
	}
	return nil;
}

static char *
setalg(Espcb *ecb, char **f, int n, Algorithm *alg)
{
	uchar *key;
	int c, i, nbyte, nchar;

	if(n < 2)
		return "bad format";
	for(; alg->name; alg++)
		if(strcmp(f[1], alg->name) == 0)
			break;
	if(alg->name == nil)
		return "unknown algorithm";

	if(n != 3)
		return "bad format";
	nbyte = (alg->keylen + 7) >> 3;
	nchar = strlen(f[2]);
	for(i=0; i<nchar; i++) {
		c = f[2][i];
		if(c >= '0' && c <= '9')
			f[2][i] -= '0';
		else if(c >= 'a' && c <= 'f')
			f[2][i] -= 'a'-10;
		else if(c >= 'A' && c <= 'F')
			f[2][i] -= 'A'-10;
		else
			return "bad character in key";
	}
	key = smalloc(nbyte);
	for(i=0; i<nchar && i*2<nbyte; i++) {
		c = f[2][nchar-i-1];
		if(i&1)
			c <<= 4;
		key[i>>1] |= c;
	}

	alg->init(ecb, alg->name, key, alg->keylen);
	free(key);
	return nil;
}

static int
nullcipher(Espcb* _, uchar* __, int ___)
{
	return 1;
}

static void
nullespinit(Espcb *ecb, char *name, uchar* _, int __)
{
	ecb->espalg = name;
	ecb->espblklen = 1;
	ecb->espivlen = 0;
	ecb->cipher = nullcipher;
}

static int
nullauth(Espcb* _, uchar* __, int ___, uchar* ____)
{
	return 1;
}

static void
nullahinit(Espcb *ecb, char *name, uchar* _, int __)
{
	ecb->ahalg = name;
	ecb->ahblklen = 1;
	ecb->ahlen = 0;
	ecb->auth = nullauth;
}

void
seanq_hmac_sha1(uchar hash[SHA1dlen], uchar *t, long tlen, uchar *key, long klen)
{
	uchar ipad[65], opad[65];
	int i;
	DigestState *digest;
	uchar innerhash[SHA1dlen];

	for(i=0; i<64; i++){
		ipad[i] = 0x36;
		opad[i] = 0x5c;
	}
	ipad[64] = opad[64] = 0;
	for(i=0; i<klen; i++){
		ipad[i] ^= key[i];
		opad[i] ^= key[i];
	}
	digest = sha1(ipad, 64, nil, nil);
	sha1(t, tlen, innerhash, digest);
	digest = sha1(opad, 64, nil, nil);
	sha1(innerhash, SHA1dlen, hash, digest);
}

static int
shaauth(Espcb *ecb, uchar *t, int tlen, uchar *auth)
{
	uchar hash[SHA1dlen];
	int r;

	memset(hash, 0, SHA1dlen);
	seanq_hmac_sha1(hash, t, tlen, (uchar*)ecb->ahstate, 16);
	r = memcmp(auth, hash, ecb->ahlen) == 0;
	memmove(auth, hash, ecb->ahlen);
	return r;
}

static void
shaahinit(Espcb *ecb, char *name, uchar *key, int klen)
{
	if(klen != 128)
		panic("shaahinit: bad keylen");
	klen >>= 8;		/* convert to bytes */

	ecb->ahalg = name;
	ecb->ahblklen = 1;
	ecb->ahlen = 12;
	ecb->auth = shaauth;
	ecb->ahstate = smalloc(klen);
	memmove(ecb->ahstate, key, klen);
}

void
seanq_hmac_md5(uchar hash[MD5dlen], uchar *t, long tlen, uchar *key, long klen)
{
	uchar ipad[65], opad[65];
	int i;
	DigestState *digest;
	uchar innerhash[MD5dlen];

	for(i=0; i<64; i++){
		ipad[i] = 0x36;
		opad[i] = 0x5c;
	}
	ipad[64] = opad[64] = 0;
	for(i=0; i<klen; i++){
		ipad[i] ^= key[i];
		opad[i] ^= key[i];
	}
	digest = md5(ipad, 64, nil, nil);
	md5(t, tlen, innerhash, digest);
	digest = md5(opad, 64, nil, nil);
	md5(innerhash, MD5dlen, hash, digest);
}

static int
md5auth(Espcb *ecb, uchar *t, int tlen, uchar *auth)
{
	uchar hash[MD5dlen];
	int r;

	memset(hash, 0, MD5dlen);
	seanq_hmac_md5(hash, t, tlen, (uchar*)ecb->ahstate, 16);
	r = memcmp(auth, hash, ecb->ahlen) == 0;
	memmove(auth, hash, ecb->ahlen);
	return r;
}

static void
md5ahinit(Espcb *ecb, char *name, uchar *key, int klen)
{
	if(klen != 128)
		panic("md5ahinit: bad keylen");
	klen >>= 3;		/* convert to bytes */

	ecb->ahalg = name;
	ecb->ahblklen = 1;
	ecb->ahlen = 12;
	ecb->auth = md5auth;
	ecb->ahstate = smalloc(klen);
	memmove(ecb->ahstate, key, klen);
}

static int
descipher(Espcb *ecb, uchar *p, int n)
{
	uchar tmp[8];
	uchar *pp, *tp, *ip, *eip, *ep;
	DESstate *ds = ecb->espstate;

	ep = p + n;
	if(ecb->incoming) {
		memmove(ds->ivec, p, 8);
		p += 8;
		while(p < ep){
			memmove(tmp, p, 8);
			block_cipher(ds->expanded, p, 1);
			tp = tmp;
			ip = ds->ivec;
			for(eip = ip+8; ip < eip; ){
				*p++ ^= *ip;
				*ip++ = *tp++;
			}
		}
	} else {
		memmove(p, ds->ivec, 8);
		for(p += 8; p < ep; p += 8){
			pp = p;
			ip = ds->ivec;
			for(eip = ip+8; ip < eip; )
				*pp++ ^= *ip++;
			block_cipher(ds->expanded, p, 0);
			memmove(ds->ivec, p, 8);
		}
	}
	return 1;
}

static void
desespinit(Espcb *ecb, char *name, uchar *k, int n)
{
	uchar key[8], ivec[8];
	int i;

	/* bits to bytes */
	n = (n+7)>>3;
	if(n > 8)
		n = 8;
	memset(key, 0, sizeof(key));
	memmove(key, k, n);
	for(i=0; i<8; i++)
		ivec[i] = nrand(256);
	ecb->espalg = name;
	ecb->espblklen = 8;
	ecb->espivlen = 8;
	ecb->cipher = descipher;
	ecb->espstate = smalloc(sizeof(DESstate));
	setupDESstate(ecb->espstate, key, ivec);
}

void
espinit(Fs *fs)
{
	Proto *esp;

	esp = smalloc(sizeof(Proto));
	esp->priv = smalloc(sizeof(Esppriv));
	esp->name = "esp";
	esp->connect = espconnect;
	esp->announce = nil;
	esp->ctl = espctl;
	esp->state = espstate;
	esp->create = espcreate;
	esp->close = espclose;
	esp->rcv = espiput;
	esp->advise = espadvise;
	esp->stats = espstats;
	esp->local = esplocal;
	esp->remote = espremote;
	esp->ipproto = IP_ESPPROTO;
	esp->nc = Nchans;
	esp->ptclsize = sizeof(Espcb);

	Fsproto(fs, esp);
}


#ifdef notdef
enum {
	RC4forward= 10*1024*1024,	/* maximum skip forward */
	RC4back = 100*1024,	/* maximum look back */
};

typedef struct Esprc4 Esprc4;
struct Esprc4
{
	ulong	cseq;		/* current byte sequence number */
	RC4state current;

	int	ovalid;		/* old is valid */
	ulong	lgseq;		/* last good sequence */
	ulong	oseq;		/* old byte sequence number */
	RC4state old;
};

static void rc4espinit(Espcb *ecb, char *name, uchar *k, int n);

static int
rc4cipher(Espcb *ecb, uchar *p, int n)
{
	Esprc4 *esprc4;
	RC4state tmpstate;
	ulong seq;
	long d, dd;

	if(n < 4)
		return 0;

	esprc4 = ecb->espstate;
	if(ecb->incoming) {
		seq = nhgetl(p);
		p += 4;
		n -= 4;
		d = seq-esprc4->cseq;
		if(d == 0) {
			rc4(&esprc4->current, p, n);
			esprc4->cseq += n;
			if(esprc4->ovalid) {
				dd = esprc4->cseq - esprc4->lgseq;
				if(dd > RC4back)
					esprc4->ovalid = 0;
			}
		} else if(d > 0) {
print("esp rc4cipher: missing packet: %uld %ld\n", seq, d); /* this link is hosed */
			if(d > RC4forward) {
				strcpy(up->errstr, "rc4cipher: skipped too much");
				return 0;
			}
			esprc4->lgseq = seq;
			if(!esprc4->ovalid) {
				esprc4->ovalid = 1;
				esprc4->oseq = esprc4->cseq;
				memmove(&esprc4->old, &esprc4->current,
					sizeof(RC4state));
			}
			rc4skip(&esprc4->current, d);
			rc4(&esprc4->current, p, n);
			esprc4->cseq = seq+n;
		} else {
print("esp rc4cipher: reordered packet: %uld %ld\n", seq, d);
			dd = seq - esprc4->oseq;
			if(!esprc4->ovalid || -d > RC4back || dd < 0) {
				strcpy(up->errstr, "rc4cipher: too far back");
				return 0;
			}
			memmove(&tmpstate, &esprc4->old, sizeof(RC4state));
			rc4skip(&tmpstate, dd);
			rc4(&tmpstate, p, n);
			return 1;
		}

		/* move old state up */
		if(esprc4->ovalid) {
			dd = esprc4->cseq - RC4back - esprc4->oseq;
			if(dd > 0) {
				rc4skip(&esprc4->old, dd);
				esprc4->oseq += dd;
			}
		}
	} else {
		hnputl(p, esprc4->cseq);
		p += 4;
		n -= 4;
		rc4(&esprc4->current, p, n);
		esprc4->cseq += n;
	}
	return 1;
}

static void
rc4espinit(Espcb *ecb, char *name, uchar *k, int n)
{
	Esprc4 *esprc4;

	/* bits to bytes */
	n = (n+7)>>3;
	esprc4 = smalloc(sizeof(Esprc4));
	memset(esprc4, 0, sizeof(Esprc4));
	setupRC4state(&esprc4->current, k, n);
	ecb->espalg = name;
	ecb->espblklen = 4;
	ecb->espivlen = 4;
	ecb->cipher = rc4cipher;
	ecb->espstate = esprc4;
}
#endif
