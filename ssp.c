/*
 *      SSP - Shell Script Profiler - Version 1.0
 *      Copyright (c) 2005 - Willy Tarreau <willy@w.ods.org>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * Usage: ssp {e(nter) | l(eave)} <funcname> <dir> [ $? ];
 * if <$?> is passed, the return code will be transferred.
 *
 * Stupid example :
 *
 *     foo() {
 *         ssp enter $FUNCNAME /tmp
 *         sleep 1
 *         ssp leave $FUNCNAME /tmp
 *         return $1
 *     }
 *
 *     bar() {
 *         ssp enter $FUNCNAME /tmp
 *         foo $2
 *         ssp leave $FUNCNAME /tmp $?
 *         return $?
 *     }
 *    
 *
 * Sort the stats: 
 *  - by # of calls: grep '' *|tr ':' ' '|sed -e 's/\.sta//'|sort -n -k2.1
 *  - by total time: grep '' *|tr ':' ' '|sed -e 's/\.sta//'|sort -n -k3.1
 *  - by time/call : grep '' *|tr ':' ' '|sed -e 's/\.sta//'|sort -n -k4.1
 */

#include <stdio.h>
#include <sys/time.h>
#include <sys/fcntl.h>

const char ssta[] = ".sta";  int lsta = sizeof(ssta);
const char sstr[] = ".str";  int lstr = sizeof(sstr);
const char slvl[] = ".lvl";  int llvl = sizeof(slvl);

#define MODE_ENTER 0
#define MODE_LEAVE 1

char *dir, *fct;
char *n_sta, *n_str, *n_lvl;
int mode;
int exit_code = 0;

// primitives
FILE *open_file(char *n) {
    int fd;
    FILE *f;
    if ((fd = open(n, O_RDWR | O_CREAT, 0600)) == -1)
	return NULL;

    if ((f = fdopen(fd, "w+")) == NULL) {
	close(fd);
	return NULL;
    }
    return f;
}

/* if file <f> is NULL, then open file whose name is in <n>, otherwise
 * use <f>. Then read <i> from the first line of the file. The file is
 * returned in all cases (NULL if any error). <i> is set to 0 if the
 * file cannot be read.
 */
FILE *read_int(FILE *f, char *n, int *i) {
    int fd;
    char s[16];

    if (f == NULL) {
	if ((f = open_file(n)) == NULL) {
	    close(fd);
	    *i = 0;
	    return NULL;
	}
    }
    else
	fseek(f, 0, SEEK_SET);

    if (fgets((char *)&s, sizeof(s), f) != NULL && *s)
	*i = atoi(s);
    else
	*i = 0;
    return f;
}

/* if file <f> is NULL, then open file whose name is in <n>, otherwise
 * use <f>. Then write <i> at the beginning of the file. The file is
 * returned in all cases (NULL if any error).
 */
FILE *write_int(FILE *f, char *n, int i) {
    int fd;
    if (f == NULL) {
	if ((f = open_file(n)) == NULL) {
	    close(fd);
	    return NULL;
	}
    }
    else
	fseek(f, 0, SEEK_SET);

    fprintf(f, "%d\n", i);
    return f;
}


// main functions
void process_enter() {
    FILE *f_lvl, *f_str;
    int lvl;

    if ((f_lvl = read_int(NULL, n_lvl, &lvl)) == NULL)
	return;
    
    lvl++;
    write_int(f_lvl, NULL, lvl);

    if (lvl == 1) {  // no recursion, count only the outer time
	struct timeval tv;
	f_str = open_file(n_str);
	gettimeofday(&tv, NULL);
	fprintf(f_str, "%u.%06u\n", tv.tv_sec, tv.tv_usec);
	fclose(f_str);
    }
    fclose(f_lvl);
}

void process_leave() {
    FILE *f_lvl, *f_str, *f_sta;
    int lvl, cnt;

    if ((f_lvl = read_int(NULL, n_lvl, &lvl)) == NULL)
	return;
    
    lvl--;
    if (lvl > 0)
	write_int(f_lvl, NULL, lvl);
    else {
	struct timeval tv1, tv2;
	int fields;
	gettimeofday(&tv2, NULL);
	f_str = open_file(n_str);
	fields = fscanf(f_str, "%u.%06u\n", &tv1.tv_sec, &tv1.tv_usec);
	fclose(f_str);
	if (fields == 2) { // %u.%u
	    tv2.tv_usec -= tv1.tv_usec;
	    tv2.tv_sec -= tv1.tv_sec;

	    while (tv2.tv_usec < 0 || tv2.tv_usec >= 1000000) {
		tv2.tv_sec--;
		tv2.tv_usec += 1000000;
	    }

	    // read previous elapsed time to add this one
	    f_sta = open_file(n_sta);
	    if (f_sta)
		fields = fscanf(f_sta, "%u %u.%06u", &cnt, &tv1.tv_sec, &tv1.tv_usec);
	    else
		fields = 0;

	    if (fields == 3) {
		tv2.tv_sec += tv1.tv_sec;
		tv2.tv_usec += tv1.tv_usec;
		cnt++;
	    }
	    else
		cnt = 1;

	    while (tv2.tv_usec >= 1000000) {
		tv2.tv_sec++;
		tv2.tv_usec -= 1000000;
	    }
	    fseek(f_sta, 0, SEEK_SET);
	    fprintf(f_sta, "%u %u.%06u %f\n", cnt, tv2.tv_sec, tv2.tv_usec,
		    ((double)tv2.tv_sec+((double)tv2.tv_usec/1000000))/cnt);
	    fclose(f_sta);

	    unlink(n_str);
	}
    }

    fclose(f_lvl);

    if (lvl == 0)
	unlink(n_lvl);
}

main(int argc, char **argv) {
    int ldir, lfct;

    if (argc < 4) {
	printf("Usage: ssp {e(nter) | l(eave)} <funcname> <dir> [$?]\n");
	exit(1);
    }

    fct = argv[2]; lfct = strlen(fct);
    dir = argv[3]; ldir = strlen(dir);
    if (argc > 4)
	exit_code = atoi(argv[4]);

    // dir "/" fct "." type
    n_sta = (char *)malloc(ldir + 1 + lfct + lsta + 1);
    sprintf(n_sta, "%s/%s%s", dir, fct, ssta);

    n_str = (char *)malloc(ldir + 1 + lfct + lstr + 1);
    sprintf(n_str, "%s/%s%s", dir, fct, sstr);

    n_lvl = (char *)malloc(ldir + 1 + lfct + llvl + 1);
    sprintf(n_lvl, "%s/%s%s", dir, fct, slvl);

    if (argv[1][0] == 'e')  // "enter"
	process_enter();
    else
	process_leave();

    return exit_code;
}

