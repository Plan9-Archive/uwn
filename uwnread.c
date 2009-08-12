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

const int INITREAD = 10;
const int MORE = 10;
const int LISTLEN = 15;

Biobuf *bin;

Panel *root, *disp, *list, *groupentry, *msg;
char *path;
char *group;
Font *font;

int narticles, nread;
Dir *db;
Article *articles;
Article *current;

void eresized(int);

void 
done(Panel *p, int buttons) 
{
	USED(p, buttons);
	free(path);
	exits(0);
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
getheaders(int n, int reget) 
{
	int fd, i, j;
	//Dir *db;
	char tmp[255];
	char *header;

	fd = open(path, OREAD);
	if (fd < 0) {
		fprint(2, "Nonexistent newsgroup %s\n", group);
		exits("open failed");
	}
	if (reget || !db) {
		message("Re-reading group...");
		narticles = dirreadall(fd, &db);
		if (nread)
			n = nread;
		nread = 0;
		message("Done.");
	}

	close(fd);

	if (n > narticles)
		n = narticles;

	articles = realloc(articles, sizeof(Article)*(nread+n));
	message("Reading headers...");
	for (i = nread, j=narticles - 1 - nread; i < narticles, j >= narticles - nread - n; i++, j--) {
		if (isdigit(db[j].name[0])) {
			articles[i].subject = "no subject";
			articles[i].from = "";
			articles[i].body = 0;
			articles[i].btext = 0;
			articles[i].msgid = "(not found)";
			articles[i].filename = strdup(path);
			strcat(articles[i].filename, db[j].name);
			snprint(tmp, 255, "%s/%s", articles[i].filename, "header");
			if (bin = Bopen(tmp, OREAD)) {
				while (header = Brdstr(bin, '\n', 1)) {
					if (cistrstr(header, "From: ") == header)
						articles[i].from = strdup(header+6);
					else if (cistrstr(header, "Subject: ") == header)
						articles[i].subject = strdup(header) + 9;
					else if (cistrstr(header, "Message-ID: ") == header)
						articles[i].msgid = strdup(header) + 12;

					free(header);
				}
				Bterm(bin);
				//print("found article %s, from %s, subject %s\n", tmp, articles[i].from, articles[i].subject);
			} else {
				i--;
				narticles--;
			}
		} else {
			i--;
			narticles--;
			if (n > narticles)
				n = narticles;
		}
	}
	message("Done.");

	nread += n;
}


void
fetchbody(Panel *, int buttons, int n) {
	int fd, space;
	char tmp[255];
	char *foo;

	message("Reading article body.");
	if (!articles[n].body) {
		articles[n].body = 0;
		foo = "";
		snprint(tmp, 255, "%s/%s", articles[n].filename, "article");
		if (bin = Bopen(tmp, OREAD)) {
			space = 0;
			while (foo = Brdstr(bin, '\n', 1)) {
				plrtstr(&articles[n].body, space, 0, font, foo, 0, 0);
				space = 1000000;
			}
			free(foo);
			Bterm(bin);
		}
	}

/*
	if (!articles[n].btext) {
		articles[n].btext = smprint("%s wrote:\n", articles[n].from);
		foo = "";
		snprint(tmp, 255, "%s/%s", articles[n].filename, "body");
		if (bin = Bopen(tmp, OREAD)) {
			while (foo = Brdstr(bin, '\n', 1)) {
				snprint(tmp, 255, ">%s\n", foo);
				articles[n].btext = strcat(articles[n].btext, tmp);
			}
			Bterm(bin);
			free(foo);
		}
		//print("%s", articles[n].btext);
	}
*/

	current = &articles[n];
	message("Done.");
	plinittextview(disp, PACKS|EXPAND, Pt(0,0), articles[n].body, 0);
	pldraw(root, screen);
}

char*
genlist(Panel *, int n) 
{
	static char retval[100];
	if (n < nread) {
		snprint(retval, 100, "%3.d : %-30.30s : %s", n, articles[n].from, articles[n].subject);
		return retval;
	} else {
		return 0;
	}
}

void
getmore(Panel *, int) 
{
	getheaders(MORE, 0);
	plinitlist(list, EXPAND|PACKE, genlist, LISTLEN, fetchbody);
	pldraw(root, screen);
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
reget(Panel *, int)
{
	Dir *dirb;

	dirb = dirstat(path);
	if (!dirb) {
		message("Cannot stat directory... bad newsgroup?");
		return;
	}

	getheaders(0, 1);
	pldraw(root, screen);
}

void
changegroup(Panel *, char *ng)
{
	char *gname;
	Dir *dirb;

	group = ng;

	gname = convgroup(ng);
	path = smprint("/mnt/news/%s/", gname);
	free(db);
	nread = 0;
	dirb = dirstat(path);
	if (!dirb) {
		message("Cannot stat directory... bad newsgroup?");
		return;
	}

	getheaders(INITREAD, 1);

	pldraw(root, screen);
}

void
post(Panel *, int)
{
	char *tmp;

	tmp = smprint("-g%s", group);
	if (!fork()) {
		execl("/bin/uwnwrite", "uwnwrite", "-w", tmp, nil);
	}
	free(tmp);
}

void
reply(Panel *, int)
{

	char *grp, *ref, *sub, *body;
	
	grp = smprint("-g%s", group);
	ref = smprint("-r%s", current->msgid);
	if (strstr(current->subject, "Re: ") != current->subject)
		sub = smprint("-sRe: %s", current->subject);
	else
		sub = smprint("-s%s", current->subject);
	body = smprint("-b%s", current->filename);
	if (!fork()) {
		execl("/bin/uwnwrite", "uwnwrite", "-w", grp, ref, sub, body, nil);
	}
	free(grp);
	free(ref);
	free(sub);
	free(body);
}

void 
main(int argc, char *argv[]) {
	Event e;
	Panel *l, *s, *p;
	char *gname, *fpath;

	if (argc == 1) {
		group = "comp.os.plan9";
		gname = "comp/os/plan9";
	} else if (argc == 2) {
		argv++;
		group = argv[0];
		gname = convgroup(group);
	} else {
		fprint(2, "Usage: %s group\n", argv[0]);
		exits("usage");
	}

	path = smprint("/mnt/news/%s/", gname);

	nread = 0;

	fpath = getenv("font");
	font = openfont(display, fpath);

	getheaders(INITREAD, 0);

	initdraw(nil, nil, "foo");
	einit(Emouse|Ekeyboard);
	plinit(screen->depth);

	root=plframe(0, EXPAND);
		p = plgroup(root, PACKN|FILLX);
			pllabel(p, PACKW, "Newsgroup:");
			groupentry = plentry(p, PACKW, 300, group, changegroup);
			plbutton(p, PACKE, "Quit", done);
		p = plgroup(root, PACKN|FILLX);
			list = pllist(p, EXPAND|PACKE, genlist, LISTLEN, fetchbody);
			s = plscrollbar(p, PACKW|FILLY);
			plscroll(list, 0, s);
		p = plgroup(root, PACKN|FILLX);
			plbutton(p, PACKW, "Post", post);
			plbutton(p, PACKW, "Reply", reply);
			plbutton(p, PACKE, "Re-read Group", reget);
			plbutton(p, PACKE, "More Headers", getmore);
		p = plgroup(root, PACKN|EXPAND);
			disp = pltextview(p, PACKE|EXPAND, Pt(0,0), 0, 0);
			s = plscrollbar(p, PACKW|FILLY);
			plscroll(disp, 0, s);
		p = plgroup(root, PACKN|FILLX);
			msg = pllabel(p, PACKW|EXPAND, "Done.");
			plplacelabel(msg, PLACEW);

	plgrabkb(groupentry);
	eresized(0);

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


