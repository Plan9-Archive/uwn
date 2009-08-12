/*
 * Copyright © 2009 John Floren
 */

#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <event.h>
#include <panel.h>
#include <ctype.h>
#include <bio.h>

typedef struct Article Article;
struct Article {
	char *subject;
	char *from;
	Rtext *body; // currently this includes the entire header too
	char *btext; // this is ugly but we want it for replying
	char *msgid;
	char *filename;
};

Panel *root, *entry, *from, *subject, *newsgroups, *references, *msg;

Article article;

void 
done(Panel *p, int buttons) {
	USED(p, buttons);
	exits(0);
}

char*
convgroup(char *c) 
{
	int i;
	char *r;

	r = strdup(c);

	for (i = 0; r[i] != '\0'; i++)
		if (r[i] == '.')
			r[i] = '/';

	return r;
}

void
message(char *c)
{
	if (screen) {
		plinitlabel(msg, PACKW|EXPAND, c);
		pldraw(msg, screen);
	}
}

void
post(Panel *p, int buttons) {
	int fd, ntok, i;
	char *tmp, *s;
	char *ng[32];

	tmp = smprint("From: %s\nNewsgroups: %s\nSubject: %s\nReferences: %s\n\n\n%.*S\n", plentryval(from), plentryval(newsgroups), plentryval(subject), plentryval(references), plelen(entry), pleget(entry));

	message("Checking postability");
	s = strdup(plentryval(newsgroups));
	ntok = gettokens(plentryval(newsgroups), ng, nelem(ng), "\t\r\n, ");
	for (i = 0; i < ntok; i++) {
		fd = open(smprint("/mnt/news/%s/post", convgroup(ng[i])), OWRITE);
		//print("%s\n", smprint("/mnt/news/%s/post", convgroup(ng[i])));
		if (fd < 0) {
			message(smprint("Newsgroup %s does not seem to have a post file. No post made.", ng[i]));
			return;
		}
		close(fd);
	}

	fd = open(smprint("/mnt/news/%s/post", convgroup(ng[0])), OWRITE);

	fprint(fd, "%s", tmp);

	close(fd);
	message("Message sent.");
}

static char*
rdenv(char *name)
{
	char *v;
	int fd, size;

	fd = open(name, OREAD);
	if(fd < 0)
		return 0;
	size = seek(fd, 0, 2);
	v = malloc(size+1);
	if(v == 0){
		fprint(2, "page: can't malloc: %r\n");
		exits("no mem");
	}
	seek(fd, 0, 0);
	read(fd, v, size);
	v[size] = 0;
	close(fd);
	return v;
}

void
newwin(void)
{
	char *srv, *mntsrv;
	char spec[100];
	int srvfd, cons, pid;

	switch(rfork(RFFDG|RFPROC|RFNAMEG|RFENVG|RFNOTEG|RFNOWAIT)){
	case -1:
		fprint(2, "page: can't fork: %r\n");
		exits("no fork");
	case 0:
		break;
	default:
		exits(0);
	}

	srv = rdenv("/env/wsys");
	if(srv == 0){
		mntsrv = rdenv("/mnt/term/env/wsys");
		if(mntsrv == 0){
			fprint(2, "page: can't find $wsys\n");
			exits("srv");
		}
		srv = malloc(strlen(mntsrv)+10);
		sprint(srv, "/mnt/term%s", mntsrv);
		free(mntsrv);
		pid  = 0;			/* can't send notes to remote processes! */
	}else
		pid = getpid();
	srvfd = open(srv, ORDWR);
	if(srvfd == -1){
		fprint(2, "page: can't open %s: %r\n", srv);
		exits("no srv");
	}
	free(srv);
	sprint(spec, "new -pid %d", pid);
	if(mount(srvfd, -1, "/mnt/wsys", 0, spec) == -1){
		fprint(2, "page: can't mount /mnt/wsys: %r (spec=%s)\n", spec);
		exits("no mount");
	}
	close(srvfd);
	unmount("/mnt/acme", "/dev");
	bind("/mnt/wsys", "/dev", MBEFORE);
	cons = open("/dev/cons", OREAD);
	if(cons==-1){
	NoCons:
		fprint(2, "page: can't open /dev/cons: %r");
		exits("no cons");
	}
	dup(cons, 0);
	close(cons);
	cons = open("/dev/cons", OWRITE);
	if(cons==-1)
		goto NoCons;
	dup(cons, 1);
	dup(cons, 2);
	close(cons);
}

Rune *strtorune(Rune *buf, char *s){
	Rune *r;
	for(r=buf;*s;r++) s+=chartorune(r, s);
	*r='\0';
	return buf;
}


void
readarticle(char *p) {
	static char tmp[128];
	Biobuf *bin;
	char *header, *foo;


	article.from = "";
	article.btext = 0;
	article.filename = strdup(p);
	snprint(tmp, 128, "%s/header", article.filename);
	if (bin = Bopen(tmp, OREAD)) {
		while (header = Brdstr(bin, '\n', 1)) {
			if (strstr(header, "From: ") == header)
				article.from = strdup(header+6);
		}
		Bterm(bin);
	} else {
		fprint(2, "couldn't open file");
		exits("no file");
	}
	snprint(tmp, 128, "%s/body", article.filename);
	article.btext = smprint("%s wrote:\n", article.from);
	foo = "";
	if (bin = Bopen(tmp, OREAD)) {
		while (foo = Brdstr(bin, '\n', 1)) {
			snprint(tmp, 128, ">%s\n", foo);
			article.btext = strcat(article.btext, tmp);
		}
		Bterm(bin);
	}
}

void 
main(int argc, char *argv[]) {
	Event e;
	char *sub, *fr, *ng, *ref, *path;
	int anum;
	Rune *inittxt;
	Panel *p, *s;
	int makenew = 0;

	sub = fr = ng = ref = "";
	path = 0;
	inittxt = L"";

	ARGBEGIN {
		case 's':
			sub = smprint("%s", ARGF());
			break;
		case 'f':
			fr = smprint("%s", ARGF());
			break;
		case 'g':
			ng = smprint("%s", ARGF());
			break;
		case 'r':
			ref = smprint("%s", ARGF());
			break;
		case 'b':
			path = smprint("%s", ARGF());
			break;
		case 'w':
			makenew = 1;
			break;
		default:
			fprint(2, "Usage: writer [-w] [-s Subject] [-f From] [-g Newsgroups] [-r Reference] [-b path]\n", argv[0]);
			exits("usage");
	} ARGEND


	if (path) {
		readarticle(path);

		inittxt = runesmprint("%s", article.btext);
//		inittxt = L"012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567891234";
//		print("%d runes, %.*S", runestrlen(inittxt), runestrlen(inittxt), inittxt);
	}

	if (makenew)
		newwin();

	initdraw(nil, nil, "foo");
	einit(Emouse|Ekeyboard);
	plinit(screen->depth);

	root=plframe(0, EXPAND);
		p = plgroup(root, PACKN|FILLX);
			plbutton(p, PACKW, "Post", post);
			plbutton(p, PACKE, "Quit", done);
		p = plgroup(root, PACKN|FILLX);
			pllabel(p, PACKW|FILLX, "Newsgroups:");
			newsgroups = plentry(p, PACKW, 300, ng, 0);
		p = plgroup(root, PACKN|FILLX);
			pllabel(p, PACKW|FILLX, "From:");
			from = plentry(p, PACKW, 300, fr, 0);
		p = plgroup(root, PACKN|FILLX);
			pllabel(p, PACKW|FILLX, "Subject:");
			subject = plentry(p, PACKW, 300, sub, 0);
		p = plgroup(root, PACKN|FILLX);
			pllabel(p, PACKW|FILLX, "References:");
			references = plentry(p, PACKW, 300, ref, 0);
		p = plgroup(root, PACKN|EXPAND);
			entry = pledit(p, PACKE|EXPAND, Pt(0,0), inittxt, runestrlen(inittxt), plgrabkb);
//			entry = pledit(p, PACKE|EXPAND, Pt(0,0), 0, 0, plgrabkb);
			s = plscrollbar(p, PACKW|FILLY);
			plscroll(entry, 0, s);
		p = plgroup(root, PACKN|FILLX);
			msg = pllabel(p, PACKW|EXPAND, "Done.");
			plplacelabel(msg, PLACEW);

	plgrabkb(newsgroups);

	eresized(0);
//	plinitedit(entry, PACKE|EXPAND, Pt(0,0), inittxt, runestrlen(inittxt), plgrabkb);

	for (;;) {
		switch(event(&e)){
		case Ekeyboard:
			plkeyboard(e.kbdc);
			break;
		case Emouse:
			plmouse(root, e.mouse);
			break;
		}
	}

}

void
eresized(int new) {
	Rectangle r;

	if(new && getwindow(display, Refnone) == -1) {
		fprint(2, "getwindow: %r\n");
		exits("getwindow");
	}
	r = screen->r;
	plpack(root, r);
	draw(screen, r, display->white, 0, ZP);
	pldraw(root, screen);
}

