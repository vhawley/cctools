/*
Copyright (C) 2012- The University of Notre Dame
This software is distributed under the GNU General Public License.
See the file COPYING for details.
*/

#include "nvpair.h"
#include "hash_table.h"
#include "debug.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

struct argument {
	int dynamic;
	char operator[32];
	char *param;
	char *val;
	struct argument *next;
};

struct deltadb {
	struct hash_table *table;
	int static_variables_only;
	struct argument *args;
};

struct deltadb * deltadb_create( const char *logdir )
{
	struct deltadb *db = malloc(sizeof(*db));
	db->table = hash_table_create(0,0);
	db->static_variables_only = 1;
	db->args = NULL;
	return db;
}

void deltadb_delete( struct deltadb *db )
{
  // should delete all nvpairs in the table here
	if(db->table) hash_table_delete(db->table);
	//if(db->logfile) fclose(db->logfile);
	free(db);
}

static int keep_object(struct argument *arg, const char *input)
{
	char *operator = arg->operator;
	if(strcmp(operator,"=")==0) {
		if(strcmp(arg->val,input)==0)
			return 1;
		else return 0;
	}
	return 0;
}

#define NVPAIR_LINE_MAX 4096

static int checkpoint_read( struct deltadb *db )
{
	FILE * file = stdin;
	if(!file) return 0;

	char firstline[NVPAIR_LINE_MAX];
	fgets(firstline, sizeof(firstline), file);
	printf("%s",firstline);

	while(1) {
		int keep = 0;
		struct nvpair *nv = nvpair_create();
		int num_pairs = nvpair_parse_stream(nv,file);
		if(num_pairs>0) {
			const char *key = nvpair_lookup_string(nv,"key");
			if(key) {
				keep = 1;
				struct argument *arg = db->args;
				while (arg!=NULL){
					const char *var = nvpair_lookup_string(nv,arg->param);
					if ( var!=NULL && keep_object(arg,var)==0 ){
						keep = 0;
						break;
					}
					arg = arg->next;
				}

				nvpair_delete(hash_table_remove(db->table,key));
				if (keep==1){
					nvpair_print_text(nv,stdout);
					hash_table_insert(db->table,key,nv);
				}

			} else debug(D_NOTICE,"no key in object create.");
		} else if (num_pairs == -1) {
			return 1;
		} else {

			break;
		}
		if (!keep)
			nvpair_delete(nv);

	}
	return 1;
}

/*
Replay a given log file into the hash table, up to the given snapshot time.
Return true if the stoptime was reached.
*/

static int log_play( struct deltadb *db )
{
	FILE *stream = stdin;
	time_t current = 0;
	struct nvpair *nv;
	int line_number = 0;
	struct hash_table *table = db->table;

	char line[NVPAIR_LINE_MAX];
	char key[NVPAIR_LINE_MAX];
	char name[NVPAIR_LINE_MAX];
	char value[NVPAIR_LINE_MAX];
	char oper;

	int notime = 1;
	while(fgets(line,sizeof(line),stream)) {

		line_number += 1;
		int keep = 0;

		if (line[0]=='.') return 0;

		int n = sscanf(line,"%c %s %s %[^\n]",&oper,key,name,value);
		if(n<1) continue;

		switch(oper) {
			case 'C':
				nv = nvpair_create();
				int res = nvpair_parse_stream(nv,stream);
				if (res>0){

					keep = 1;
					struct argument *arg = db->args;
					while (arg!=NULL){
						const char *var = nvpair_lookup_string(nv,arg->param);
						if ( var!=NULL && keep_object(arg,var)==0 ){
							keep = 0;
							break;
						}
						arg = arg->next;
					}

					nvpair_delete(hash_table_remove(db->table,key));
					if (keep==1){
						if (notime){
							printf("T %lld\n",(long long)current);
							notime = 0;
						}
						nvpair_print_text(nv,stdout);
						hash_table_insert(db->table,key,nv);
					}

				}
				if (!keep)
					nvpair_delete(nv);
				break;
			case 'D':
				nv = hash_table_remove(table,key);
				if(nv){
					if (notime){
						printf("T %lld\n",(long long)current);
						notime = 0;
					}
					printf("%s",line);
				}//nvpair_delete(nv);
				break;
			case 'U':
				nv = hash_table_lookup(table,key);
				if(nv){
					if (notime){
						printf("T %lld\n",(long long)current);
						notime = 0;
					}
					printf("%s",line);
				}//nvpair_insert_string(nv,name,value);
				break;
			case 'R':
				nv = hash_table_lookup(table,key);
				if(nv){
					if (notime){
						printf("T %lld\n",(long long)current);
						notime = 0;
					}
					printf("%s",line);
				}//nvpair_remove(nv,name);
				break;
			case 'T':
				current = atol(key);
				notime = 1;
				break;
			default:
				debug(D_NOTICE,"corrupt log data[%i]: %s",line_number,line);
				fflush(stderr);
				break;
		}
	}
	return 1;
}

/*
Play the log from start_time to end_time by opening the appropriate
checkpoint file and working ahead in the various log files.
*/

static int parse_input( struct deltadb *db )
{
	checkpoint_read(db);

	printf(".Checkpoint End.\n");

	while(1) {

		int keepgoing = log_play(db);

		if(!keepgoing) break;

	}

	printf(".Log End.\n");

	return 1;
}

int main( int argc, char *argv[] )
{
	struct deltadb *db = deltadb_create(NULL);

	int i;
	for (i=1; i<argc; i++){
		struct argument arg;

		arg.param = argv[i];
		char *delim = strpbrk(argv[i], "<>=!");
		arg.val = strpbrk(delim, "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");

		int operator_size = (int)(arg.val-delim);
		strncpy(arg.operator,delim,operator_size);
		arg.operator[operator_size] = '\0';
		delim[0] = '\0';

		/*char *dop = strstr(arg.operator,"...");
		if ( dop!=NULL ){
			arg.dynamic = 1;
			dop[0] = '\0';
			db->static_variables_only = 0;
		} else arg.dynamic = 0;*/

		arg.next = db->args;
		db->args = &arg;
	}



	parse_input(db);

	deltadb_delete(db);

	return 0;
}
