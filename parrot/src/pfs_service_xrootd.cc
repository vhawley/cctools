/*
Copyright (C) 2003-2004 Douglas Thain and the University of Wisconsin
Copyright (C) 2005- The University of Notre Dame
This software is distributed under the GNU General Public License.
See the file COPYING for details.
*/

/*
Important: This define must come before all include files,
in order to indicate that we are working with the 64-bit definition
of file sizes in struct stat and other places as needed by xrootd.
Note that we do not define it globally, so that we are sure not to break other
people's include files.  Note also that we are careful to use
struct pfs_stat in our own headers, so that our own code isn't
sensitive to this setting.
*/

#define _FILE_OFFSET_BITS 64

#ifdef HAS_XROOTD

#include "pfs_service.h"

extern "C" {
#include "debug.h"
#include "stringtools.h"
#include "domain_name.h"
#include "link.h"
#include "file_cache.h"
#include "full_io.h"
#include "http_query.h"
}
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <pfs_types.h>
#include <string.h>
#include <XrdPosix/XrdPosixExtern.hh>

#define XROOTD_FILE_MODE (S_IFREG | 0555)
#define XROOTD_DEFAULT_PORT 1094

char *translate_file_to_xrootd(pfs_name * name)
{
	char file_buf[PFS_PATH_MAX];
	int string_length = 0;
	int port_number = XROOTD_DEFAULT_PORT;

	if(name->port != 0) {
		port_number = name->port;
	}

	string_length = sprintf(file_buf, "root://%s:%i/%s", name->host, port_number, name->rest);

	return strdup(file_buf);

}


class pfs_file_xrootd : public pfs_file {
private:
	int file_handle;

public:
	pfs_file_xrootd(pfs_name * n, int file_handle):pfs_file(n) {
		this->file_handle = file_handle;
	}

	virtual int close() {
		debug(D_XROOTD, "Closing file %i", this->file_handle);
		return XrdPosix_Close(this->file_handle);
	}

	virtual pfs_ssize_t read(void *d, pfs_size_t length, pfs_off_t offset) {
		debug(D_XROOTD, "Reading file %i", this->file_handle);
		return XrdPosix_Read(this->file_handle, d, length);
	}

	virtual int fstat(struct pfs_stat *buf) {
		debug(D_XROOTD, "fstating file %i", this->file_handle);
		struct stat lbuf;
	
		if(XrdPosix_Fstat(this->file_handle,&lbuf)) {
			COPY_STAT(lbuf,*buf);
			return 0;
		} else {
			return 1;
		}
	}

	virtual pfs_ssize_t get_size() {
		struct pfs_stat buf;
		if(this->fstat(&buf)==0) {
			return buf.st_size;
		} else {
			return -1;
		}
	}
};

class pfs_service_xrootd:public pfs_service {
public:
	virtual pfs_file * open(pfs_name * name, int flags, mode_t mode) {
		int file_handle;
		char *file_url = NULL;

		  debug(D_XROOTD, "Opening file: %s", name->rest);

		if((flags & O_ACCMODE) != O_RDONLY) {
			errno = EROFS;
			return 0;
		}

		file_url = translate_file_to_xrootd(name);
		file_handle = XrdPosix_Open(file_url, flags, mode);
		free(file_url);

		if(file_handle) {
			return new pfs_file_xrootd(name, file_handle);
		} else {
			return 0;
		}

	}

	virtual int stat(pfs_name * name, struct pfs_stat *buf) {
		struct stat lbuf;
		char *file_url = NULL;
		int to_return;

		debug(D_XROOTD, "Stating xrootd file: %s", name->rest);
		file_url = translate_file_to_xrootd(name);
		to_return = XrdPosix_Stat((const char *) file_url, &lbuf);
		free(file_url);

		COPY_STAT(lbuf, *buf);

		return to_return;

	}

	virtual int lstat(pfs_name * name, struct pfs_stat *buf) {
		return this->stat(name, buf);
	}
};

static pfs_service_xrootd pfs_service_xrootd_instance;
pfs_service *pfs_service_xrootd = &pfs_service_xrootd_instance;

#endif
