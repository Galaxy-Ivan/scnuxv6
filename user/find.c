#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
	int plen = strlen(p);
  memmove(buf, p, plen);
  memset(buf+plen, ' ', DIRSIZ-plen);
  return buf;
}

void
find(char *root, char *fc) {
	char buf[512], *p;
	char fcBuf[DIRSIZ + 1];
	// char fileBuf[DIRSIZ + 1];
	memmove(fcBuf, fmtname(fc), DIRSIZ);
	int fd = open(root, 0);
	struct dirent de;
	struct stat st;
	if (fd < 0) {
		fprintf(2, "find: cannot open %s\n", root);
		return;
	}
	if (fstat(fd, &st) < 0) {
		fprintf(2, "find: cannot stat %s\n", root);
		close(fd);
		return;
	}

	strcpy(buf, root);
	if (st.type == T_FILE) {
		//printf("dbgA {%s}, {%s}\n", fmtname(root));
		if (strcmp(fmtname(root), fcBuf) == 0) {
			printf("%s\n", root);
		}
	}
	else if (st.type == T_DIR) {
		p = buf + strlen(buf);
		// printf("dbg run here, path = %s\n", root);
		while (read(fd, &de, sizeof(de)) == sizeof(de)) {
			if (de.inum == 0)
				continue;
			// printf("dbg2 %s\n", buf);
			p[0] = '/';
			if (strcmp(de.name, ".") == 0)
				continue;
			if (strcmp(de.name, "..") == 0)
				continue;
			memmove(p + 1, de.name, DIRSIZ);
			p[DIRSIZ + 1] = '\0';
			find(buf, fc);
		}
	}
	close(fd);
}

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		fprintf(2, "usage: find <path> <findname>\n");
		exit(1);
	}
	find(argv[1], argv[2]);
	exit(0);
}