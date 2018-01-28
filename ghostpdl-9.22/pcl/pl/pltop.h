/* Copyright (C) 2001-2012 Artifex Software, Inc.
   All Rights Reserved.

   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied,
   modified or distributed except as expressly authorized under the terms
   of the license contained in the file LICENSE in this distribution.

   Refer to licensing information at http://www.artifex.com or contact
   Artifex Software, Inc.,  7 Mt. Lassen Drive - Suite A-134, San Rafael,
   CA  94903, U.S.A., +1(415)492-9861, for further information.
*/


/* pltop.h */
/* API for language interpreters */


#ifndef pltop_INCLUDED
#  define pltop_INCLUDED

#include "gsgc.h"
#include "scommon.h"

#ifndef gx_device_DEFINED
#  define gx_device_DEFINED
typedef struct gx_device_s gx_device;
#endif

/*
 * Generic interpreter data types which may be subclassed by specific interpereters
 */
typedef struct pl_interp_implementation_s pl_interp_implementation_t;   /* fwd decl */

/*
 * Implementation characteristics descriptor
 */
typedef struct pl_interp_characteristics_s
{
    const char *language;       /* generic language should correspond with
                                   HP documented PJL name */
    const char *auto_sense_string;      /* string used to detect language */
    const char *manufacturer;   /* manuf str */
    const char *version;        /* version str */
    const char *build_date;     /* build date str */
    int min_input_size;         /* min sizeof input buffer */
} pl_interp_characteristics_t;

/*
 * Function to return the characteristics to the main loop.
 */
const pl_interp_characteristics_t *pl_characteristics(const
                                                      pl_interp_implementation_t
                                                      *);
typedef const pl_interp_characteristics_t
    *(*pl_interp_proc_characteristics_t) (const pl_interp_implementation_t *);

/*
 * Allocate language client data.
 */
int pl_allocate_interp_instance(pl_interp_implementation_t *, gs_memory_t *);
typedef int (*pl_interp_proc_allocate_interp_instance_t) (pl_interp_implementation_t *,
                                                          gs_memory_t *);

/*
 * Set a device, possibly shared, into the graphics state of the language.
 */

int pl_set_device(pl_interp_implementation_t *, gx_device *);
typedef int (*pl_interp_proc_set_device_t) (pl_interp_implementation_t *,
                                            gx_device *);

/*
 * Work to be done when a job begins.
 */
int pl_init_job(pl_interp_implementation_t *);
typedef int (*pl_interp_proc_init_job_t) (pl_interp_implementation_t *);

/*
 * Process a stream of PDL data.
 */
int pl_process(pl_interp_implementation_t *, stream_cursor_read *);
typedef int (*pl_interp_proc_process_t) (pl_interp_implementation_t *,
                                         stream_cursor_read *);

/*
 * The process_file function is an optional optimized path for
 * languages that want to use a random access file. If this function
 * is called for a job, pl_process, pl_flush_to_eoj and
 * pl_process_eof are not called.
 */
int pl_process_file(pl_interp_implementation_t *, char *);
typedef int (*pl_interp_proc_process_file_t) (pl_interp_implementation_t *, char *);

/*
 * Process and ignore all data until an end of job delimiter is
 * reached or end of data.
 */
int pl_flush_to_eoj(pl_interp_implementation_t *, stream_cursor_read *);
typedef int (*pl_interp_proc_flush_to_eoj_t) (pl_interp_implementation_t *,
                                              stream_cursor_read *);

/*
 * Actions to be be performed upon end of file.
 */
int pl_process_eof(pl_interp_implementation_t *);
typedef int (*pl_interp_proc_process_eof_t) (pl_interp_implementation_t *);

/*
 * Perform any error reporting.
 */
int pl_report_errors(pl_interp_implementation_t *, int, long, bool);
typedef int (*pl_interp_proc_report_errors_t) (pl_interp_implementation_t *, int,
                                               long, bool);

/*
 * Actions associated with ending a job
 */
int pl_dnit_job(pl_interp_implementation_t *);
typedef int (*pl_interp_proc_dnit_job_t) (pl_interp_implementation_t *);

/*
 * Remove the device from the graphics state and reset.
 */
int pl_remove_device(pl_interp_implementation_t *);
typedef int (*pl_interp_proc_remove_device_t) (pl_interp_implementation_t *);

/*
 * Free everything.
 */
int pl_deallocate_interp_instance(pl_interp_implementation_t *);
typedef int (*pl_interp_proc_deallocate_interp_instance_t) (pl_interp_implementation_t *);

/*
 * Define a generic interpreter implementation
 */
struct pl_interp_implementation_s
{
    /* Procedure vector */
    pl_interp_proc_characteristics_t proc_characteristics;
    pl_interp_proc_allocate_interp_instance_t proc_allocate_interp_instance;
    pl_interp_proc_set_device_t proc_set_device;
    pl_interp_proc_init_job_t proc_init_job;
    pl_interp_proc_process_file_t proc_process_file;
    pl_interp_proc_process_t proc_process;
    pl_interp_proc_flush_to_eoj_t proc_flush_to_eoj;
    pl_interp_proc_process_eof_t proc_process_eof;
    pl_interp_proc_report_errors_t proc_report_errors;
    pl_interp_proc_dnit_job_t proc_dnit_job;
    pl_interp_proc_remove_device_t proc_remove_device;
    pl_interp_proc_deallocate_interp_instance_t
        proc_deallocate_interp_instance;
    void *interp_client_data;
};

#endif /* pltop_INCLUDED */
