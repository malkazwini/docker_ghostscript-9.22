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

/* psitop.c */
/* Top-level API implementation of PS Language Interface */

#include "stdio_.h"
#include "ghost.h"
#include "imain.h"
#include "imainarg.h"
#include "iapi.h"
#include "string_.h"
#include "gdebug.h"
#include "gp.h"
#include "gserrors.h"
#include "gstypes.h"
#include "gsmemory.h"
#include "gsmalloc.h"
#include "gsstate.h"		/* must precede gsdevice.h */
#include "gxdevice.h"		/* must precede gsdevice.h */
#include "gsdevice.h"
#include "icstate.h"		/* for i_ctx_t */
#include "iminst.h"
#include "gsstruct.h"		/* for gxalloc.h */
#include "gspaint.h"
#include "gxalloc.h"
#include "gxstate.h"
#include "plparse.h"
#include "pltop.h"
#include "gzstate.h"
#include "gsicc_manage.h"

/* Forward decls */

/************************************************************/
/******** Language wrapper implementation (see pltop.h) *****/
/************************************************************/

/* Import operator procedures */
extern int zflush(i_ctx_t *);

/*
 * PS interpreter instance: derived from pl_interp_implementation_t
 */
typedef struct ps_interp_instance_s {
    pl_interp_implementation_t pl;
} ps_interp_instance_t;

/* Get implemtation's characteristics */
static const pl_interp_characteristics_t * /* always returns a descriptor */
ps_impl_characteristics(
  const pl_interp_implementation_t *impl     /* implementation of interpereter to alloc */
)
{
    /* version and build date are not currently used */
#define PSVERSION NULL
#define PSBUILDDATE NULL
  static const pl_interp_characteristics_t ps_characteristics = {
    "POSTSCRIPT",
    /* NOTE - we don't look for %! because we want to recognize pdf as well */
    "%",
    "Artifex",
    PSVERSION,
    PSBUILDDATE,
    1				/* minimum input size to PostScript */
  };
#undef PSVERSION
#undef PSBUILDDATE

  return &ps_characteristics;
}

/* Do per-instance interpreter allocation/init. */
static int
ps_impl_allocate_interp_instance(pl_interp_implementation_t *impl, gs_memory_t *mem)
{
    ps_interp_instance_t *psi
        = (ps_interp_instance_t *)gs_alloc_bytes( mem,
                                                  sizeof(ps_interp_instance_t),
                                                  "ps_impl_allocate_interp_instance");

    int code;
    
    if (!psi)
        return gs_error_VMerror;
        
    code = gsapi_new_instance(&impl->interp_client_data, NULL);
    if (code < 0)
        gs_free_object(mem, psi, "ps_impl_allocate_interp_instance");
    return code;
}

/* Set a device into an interpreter instance */
static int
ps_impl_set_device(pl_interp_implementation_t *impl, gx_device *device)
{
    /* Nothing to PS/PDF manages it's own device */
    return 0;
}


/* Prepare interp instance for the next "job" */
static int
ps_impl_init_job(pl_interp_implementation_t *impl)
{
    return gsapi_init_with_args(impl->interp_client_data, 0, NULL);
}

/* Not complete. */
static int
ps_impl_process_file(pl_interp_implementation_t *impl, char *filename)
{
    /* NB incomplete */
    int exit_code;
    return gsapi_run_file(impl->interp_client_data, filename, 0, &exit_code);
}

/* Not implemented */
static int
ps_impl_flush_to_eoj(pl_interp_implementation_t *impl, stream_cursor_read *cursor)
{
    return 0;
}

/* Parser action for end-of-file */
static int
ps_impl_process_eof(pl_interp_implementation_t *impl)
{
    return 0;
}

/* Report any errors after running a job */
static int
ps_impl_report_errors(pl_interp_implementation_t *impl,      /* interp instance to wrap up job in */
                      int                  code,           /* prev termination status */
                      long                 file_position,  /* file position of error, -1 if unknown */
                      bool                 force_to_cout    /* force errors to cout */
)
{
    return 0;
}

/* Wrap up interp instance after a "job" */
static int
ps_impl_dnit_job(pl_interp_implementation_t *impl)
{
    return 0;
}

/* Remove a device from an interpreter instance */
static int
ps_impl_remove_device(pl_interp_implementation_t *impl)
{
    return 0;
}

/* Deallocate a interpreter instance */
static int
ps_impl_deallocate_interp_instance(pl_interp_implementation_t *impl)
{

    int code = gsapi_exit(impl->interp_client_data);
    gsapi_delete_instance(impl->interp_client_data);
    return code;
}

/* Parser implementation descriptor */
const pl_interp_implementation_t ps_implementation = {
  ps_impl_characteristics,
  ps_impl_allocate_interp_instance,
  ps_impl_set_device,
  ps_impl_init_job,
  ps_impl_process_file,
  NULL, /* process */
  ps_impl_flush_to_eoj,
  ps_impl_process_eof,
  ps_impl_report_errors,
  ps_impl_dnit_job,
  ps_impl_remove_device,
  ps_impl_deallocate_interp_instance,
  NULL
};
