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


/* pjparsei.c   PJL parser implementation glue file (To pjparse.c) */

#include "string_.h"
#include "gserrors.h"
#include "pjtop.h"
#include "pjparse.h"
#include "plparse.h"
#include "plver.h"

/* Get implemtation's characteristics */
static const pl_interp_characteristics_t *      /* always returns a descriptor */
pjl_impl_characteristics(const pl_interp_implementation_t * impl        /* implementation of interpereter to alloc */
    )
{
    static const pl_interp_characteristics_t pjl_characteristics = {
        "PJL",
        "@PJL",
        "Artifex",
        PJLVERSION,
        PJLBUILDDATE,
        17                      /* sizeof min buffer == sizeof UEL */
    };
    return &pjl_characteristics;
}

/* Do per-instance interpreter allocation/init. No device is set yet */
static int                      /* ret 0 ok, else -ve error code */
pjl_impl_allocate_interp_instance(pl_interp_implementation_t *impl,
                                  gs_memory_t * mem     /* allocator to allocate instance from */
    )
{
    pjl_parser_state *pjls = pjl_process_init(mem);

    impl->interp_client_data = pjls;
    
    if (pjls == NULL)
        return gs_error_VMerror;

    /* Return success */
    return 0;
}

/* Set a device into an interperter instance */
static int                      /* ret 0 ok, else -ve error code */
pjl_impl_set_device(pl_interp_implementation_t *impl,    /* interp instance to use */
                    gx_device * device  /* device to set (open or closed) */
    )
{
    return 0;
}

/* Prepare interp instance for the next "job" */
static int                      /* ret 0 ok, else -ve error code */
pjl_impl_init_job(pl_interp_implementation_t *impl       /* interp instance to start job in */
    )
{
    pjl_parser_state *pjls = impl->interp_client_data;
    if (pjls == NULL)
        return gs_error_VMerror;
    /* copy the default state to the initial state */
    pjl_set_init_from_defaults(pjls);
    return 0;
}

/* Parse a cursor-full of data */

/* The parser reads data from the input
 * buffer and returns either:
 *	>=0 - OK, more input is needed.
 *	e_ExitLanguage - Non-PJL was detected.
 *	<0 value - an error was detected.
 */

static int
pjl_impl_process(pl_interp_implementation_t *impl,       /* interp instance to process data job in */
                 stream_cursor_read * cursor    /* data to process */
    )
{
    pjl_parser_state *pjls = impl->interp_client_data;
    int code = pjl_process(pjls, NULL, cursor);

    return code == 1 ? e_ExitLanguage : code;
}

/* Skip to end of job ret 1 if done, 0 ok but EOJ not found, else -ve error code */
static int
pjl_impl_flush_to_eoj(pl_interp_implementation_t * impl,  /* interp impl to flush for */
                      stream_cursor_read * cursor       /* data to process */
    )
{
    return pjl_skip_to_uel(cursor) ? 1 : 0;
}

/* Parser action for end-of-file */
static int                      /* ret 0 or +ve if ok, else -ve error code */
pjl_impl_process_eof(pl_interp_implementation_t * impl    /* interp impl to process data job in */
    )
{
    return 0;
}

/* Report any errors after running a job */
static int                      /* ret 0 ok, else -ve error code */
pjl_impl_report_errors(pl_interp_implementation_t * impl, /* interp impl to wrap up job in */
                       int code,        /* prev termination status */
                       long file_position,      /* file position of error, -1 if unknown */
                       bool force_to_cout       /* force errors to cout */
    )
{
    return 0;
}

/* Wrap up interp impl after a "job" */
static int                      /* ret 0 ok, else -ve error code */
pjl_impl_dnit_job(pl_interp_implementation_t * impl       /* interp impl to wrap up job in */
    )
{
    return 0;
}

/* Remove a device from an interperter impl */
static int                      /* ret 0 ok, else -ve error code */
pjl_impl_remove_device(pl_interp_implementation_t * impl  /* interp impl to use */
    )
{
    return 0;
}

/* Deallocate a interpreter impl */
static int                      /* ret 0 ok, else -ve error code */
pjl_impl_deallocate_interp_instance(pl_interp_implementation_t * impl     /* impl to dealloc */
    )
{
    pjl_parser_state *pjls = impl->interp_client_data;
    pjl_process_destroy(pjls);
    return 0;
}

/* Parser implementation descriptor */
pl_interp_implementation_t pjl_implementation = {
    pjl_impl_characteristics,
    pjl_impl_allocate_interp_instance,
    pjl_impl_set_device,
    pjl_impl_init_job,
    NULL,                      /* process_file */
    pjl_impl_process,
    pjl_impl_flush_to_eoj,
    pjl_impl_process_eof,
    pjl_impl_report_errors,
    pjl_impl_dnit_job,
    pjl_impl_remove_device,
    pjl_impl_deallocate_interp_instance,
    NULL, /* instance */
};
