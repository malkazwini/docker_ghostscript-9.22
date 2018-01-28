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


/* plmain.c */
/* Main program command-line interpreter for PCL interpreters */
#include "string_.h"
#include <stdlib.h> /* atof */
#include "gdebug.h"
#include "gscdefs.h"
#include "gsio.h"
#include "gstypes.h"
#include "gserrors.h"
#include "gsmemory.h"
#include "gsmalloc.h"
#include "gsmchunk.h"
#include "gsstruct.h"
#include "gxalloc.h"
#include "gsalloc.h"
#include "gsargs.h"
#include "gp.h"
#include "gsdevice.h"
#include "gxdevice.h"
#include "gxdevsop.h"       /* for gxdso_* */
#include "gxclpage.h"
#include "gdevprn.h"
#include "gsparam.h"
#include "gslib.h"
#include "pjtop.h"
#include "plparse.h"
#include "plmain.h"
#include "pltop.h"
#include "plcursor.h"
#include "plapi.h"
#include "gslibctx.h"
#include "gsicc_manage.h"

/* includes for the display device */
#include "gdevdevn.h"
#include "gsequivc.h"
#include "gdevdsp.h"
#include "gdevdsp2.h"

/* Include the extern for the device list. */
extern_gs_lib_device_list();

/* Extern for PDL(s): currently in one of: plimpl.c (XL & PCL), */
/* pcimpl.c (PCL only), or pximpl (XL only) depending on make configuration.*/
extern pl_interp_implementation_t *pdl_implementations[];    /* zero-terminated list */

/* Define the usage message. */
static const char *pl_usage = "\
Usage: %s [option* file]+...\n\
Options: -dNOPAUSE -E[#] -h -L<PCL|PCLXL> -K<maxK> -l<PCL5C|PCL5E|RTL> -Z...\n\
         -sDEVICE=<dev> -g<W>x<H> -r<X>[x<Y>] -d{First|Last}Page=<#>\n\
         -H<l>x<b>x<r>x<t> -dNOCACHE\n\
         -sOutputFile=<file> (-s<option>=<string> | -d<option>[=<value>])*\n\
         -J<PJL commands>\n";

/*
 * Main instance for all interpreters.
 */
struct pl_main_instance_s
{
    /* The following are set at initialization time. */
    gs_memory_t *memory;
    long base_time[2];          /* starting time */
    int error_report;           /* -E# */
    bool pause;                 /* -dNOPAUSE => false */
    int first_page;             /* -dFirstPage= */
    int last_page;              /* -dLastPage= */
    gx_device *device;

    pl_interp_implementation_t *implementation; /*-L<Language>*/

    char pcl_personality[6];    /* a character string to set pcl's
                                   personality - rtl, pcl5c, pcl5e, and
                                   pcl == default.  NB doesn't belong here. */
    bool interpolate;
    bool nocache;
    bool page_set_on_command_line;
    bool res_set_on_command_line;
    bool high_level_device;
#ifndef OMIT_SAVED_PAGES_TEST
    bool saved_pages_test_mode;
#endif
    bool pjl_from_args; /* pjl was passed on the command line */
    int scanconverter;
    /* we have to store these in the main instance until the languages
       state is sufficiently initialized to set the parameters. */
    char *piccdir;
    char *pdefault_gray_icc;
    char *pdefault_rgb_icc;
    char *pdefault_cmyk_icc;
    gs_c_param_list params;
    arg_list args;
    pl_interp_implementation_t **implementations;
    pl_interp_implementation_t *curr_implementation;
    pl_interp_implementation_t *desired_implementation;
    byte buf[8192]; /* languages read buffer */
    void *disp; /* display device pointer NB wrong - remove */
};


/* ---------------- Static data for memory management ------------------ */

static gs_gc_root_t device_root;


/* ---------------- Forward decls ------------------ */

static int pl_main_languages_init(gs_memory_t * mem, pl_main_instance_t * inst);

static pl_interp_implementation_t
    *pl_select_implementation(pl_interp_implementation_t * pjl_instance,
                              pl_main_instance_t * pmi, pl_top_cursor_t r);

/* Process the options on the command line. */
static FILE *pl_main_arg_fopen(const char *fname, void *ignore_data);

/* Process the options on the command line, including making the
   initial device and setting its parameters.  */
static int pl_main_process_options(pl_main_instance_t * pmi, arg_list * pal,
                                   pl_interp_implementation_t * pjl_instance);

static pl_interp_implementation_t *pl_pjl_select(pl_main_instance_t * pmi, pl_interp_implementation_t *pjl_instance);

/* return index in gs device list -1 if not found */
static inline int
get_device_index(const gs_memory_t * mem, const char *value)
{
    const gx_device *const *dev_list;
    int num_devs = gs_lib_device_list(&dev_list, NULL);
    int di;

    for (di = 0; di < num_devs; ++di)
        if (!strcmp(gs_devicename(dev_list[di]), value))
            break;
    if (di == num_devs) {
        errprintf(mem, "Unknown device name %s.\n", value);
        return -1;
    }
    return di;
}

int
pl_main_set_display_callback(pl_main_instance_t *inst, void *callback)
{
    inst->disp = callback;
    return 0;
}

int
pl_main_init_with_args(pl_main_instance_t *inst, int argc, char *argv[])
{
    gs_memory_t *mem = inst->memory;
    int (*arg_get_codepoint) (FILE * file, const char **astr) = NULL;
    pl_interp_implementation_t *pjli;
    
    gp_init();
    /* debug flags we reset this out of gs_lib_init0 which sets these
       and the allocator we want the debug setting but we do our own
       allocator */
#ifdef PACIFY_VALGRIND
    VALGRIND_HG_DISABLE_CHECKING(gs_debug, 128);
#endif
    memset(gs_debug, 0, 128);
    gs_log_errors = 0;

    if (gs_lib_init1(mem) < 0)
        return -1;

    {
        /*
         * gs_iodev_init has to be called here (late), rather than
         * with the rest of the library init procedures, because of
         * some hacks specific to MS Windows for patching the
         * stdxxx IODevices.
         */
        extern int gs_iodev_init(gs_memory_t *);

        if (gs_iodev_init(mem) < 0)
            return gs_error_Fatal;
    }
    
    gp_get_realtime(inst->base_time);
    gs_c_param_list_write(&inst->params, mem);
    gs_param_list_set_persistent_keys((gs_param_list *)&inst->params, false);

#if defined(__WIN32__) && !defined(GS_NO_UTF8)
    arg_get_codepoint = gp_local_arg_encoding_get_codepoint;
#endif
    arg_init(&inst->args, (const char **)argv, argc, pl_main_arg_fopen, NULL,
             arg_get_codepoint, mem);

    /* Create PDL instances, etc */
    if (pl_main_languages_init(mem, inst) < 0) {
        return gs_error_Fatal;
    }

    inst->curr_implementation = pjli = inst->implementations[0];
    
    /* initialize pjl, needed for option processing. */
    if (pl_init_job(pjli) < 0) {
        return gs_error_Fatal;
    }

    if (argc == 1 ||
        pl_main_process_options(inst,
                                &inst->args,
                                pjli) < 0) {
        /* Print error verbage and return */
        int i;
        const gx_device **dev_list;
        int num_devs =
            gs_lib_device_list((const gx_device * const **)&dev_list,
                               NULL);
        errprintf(mem, pl_usage, argv[0]);

        if (pl_characteristics(pjli)->version)
            errprintf(mem, "Version: %s\n",
                      pl_characteristics(pjli)->version);
        if (pl_characteristics(pjli)->build_date)
            errprintf(mem, "Build date: %s\n",
                      pl_characteristics(pjli)->
                      build_date);
        errprintf(mem, "Devices:");
        for (i = 0; i < num_devs; ++i) {
            if (((i + 1)) % 9 == 0)
                errprintf(mem, "\n");
            errprintf(mem, " %s", gs_devicename(dev_list[i]));
        }
        errprintf(mem, "\n");
        return gs_error_Fatal;
    }
    return 0;
}


int
pl_main_run_string_begin(void *instance)
{
    return 0;
}

int
pl_main_run_string_continue(void *instance, const char *str, unsigned int length)
{
    return 0;
}

int
pl_main_run_string_end(void *instance)
{
    return 0;
}

int
pl_main_run_file(pl_main_instance_t *minst, const char *filename)
{
    bool new_job = true;
    pl_interp_implementation_t *pjli =
        minst->implementations[0];
    gs_memory_t *mem = minst->memory;
    pl_top_cursor_t r;
    int code = 0;

#define curr_implementation minst->curr_implementation
#define desired_implementation minst->desired_implementation
    
    if (pl_cursor_open(mem, &r, filename, minst->buf, sizeof(minst->buf)) < 0) {
        return gs_error_Fatal;
    }

    for (;;) {
        if_debug1m('I', mem, "[i][file pos=%ld]\n",
                   pl_cursor_position(&r));

        if (pl_cursor_next(&r) <= 0) {
            if_debug0m('I', mem, "End of of data\n");
            pl_process_eof(curr_implementation);
            if (pl_dnit_job(curr_implementation) < 0)
                return gs_error_Fatal;
            break;
        }
        
        if (new_job) {
            if_debug0m('I', mem, "Selecting PDL\n");
            desired_implementation = pl_select_implementation(pjli, minst, r);
            if (curr_implementation != desired_implementation) {
                code = pl_remove_device(curr_implementation);
                if (code >= 0)
                    code = pl_set_device(desired_implementation, minst->device);
                if (code < 0)
                    return gs_error_Fatal;
            }
            
            curr_implementation = desired_implementation;

            /* Don't reset PJL if there is PJL state from the command line arguments. */
            if (curr_implementation == pjli) {
                if (minst->pjl_from_args == false) {
                    if (pl_init_job(pjli) < 0)
                        return gs_error_Fatal;
                } else
                    minst->pjl_from_args = false;
            } else {
                if (pl_init_job(curr_implementation) < 0)
                    return gs_error_Fatal;
            }
            
            if_debug1m('I', mem, "selected and initializing (%s)\n",
                       pl_characteristics(curr_implementation)->language);
            new_job = false;
        }

        if (curr_implementation) {
            /* Special case when the job resides in a seekable file and
               the implementation has a function to process a file at a
               time. */
            if (curr_implementation->proc_process_file
                && r.strm != mem->gs_lib_ctx->fstdin) {
                if_debug1m('I', mem, "processing job from file (%s)\n",
                           filename);

                code = pl_process_file(curr_implementation, (char *)filename);
                if (code < 0) {
                    errprintf(mem, "Warning interpreter exited with error code %d\n",
                              code);
                }
                if (pl_dnit_job(curr_implementation) < 0)
                    return gs_error_Fatal;

                break;      /* break out of the loop to process the next file */
            }

            code = pl_process(curr_implementation, &r.cursor);
            if_debug1m('I', mem, "processing (%s) job\n",
                       pl_characteristics(curr_implementation)->language);
            if (code == e_ExitLanguage) {
                if (pl_dnit_job(curr_implementation) < 0)
                    return gs_error_Fatal;

                new_job = true;

                if (curr_implementation != pjli)
                    if (pl_init_job(pjli) < 0)
                        return gs_error_Fatal;

            } else if (code < 0) {  /* error and not exit language */
                dmprintf1(mem,
                          "Warning interpreter exited with error code %d\n",
                          code);
                dmprintf(mem, "Flushing to end of job\n");
                /* flush eoj may require more data */
                while ((pl_flush_to_eoj(curr_implementation, &r.cursor)) == 0) {
                    if (pl_cursor_next(&r) <= 0) {
                        if_debug0m('I', mem,
                                   "end of data found while flushing\n");
                        break;
                    }
                }
                pl_report_errors(curr_implementation, code,
                                 pl_cursor_position(&r),
                                 minst->error_report > 0);
                if (pl_dnit_job(curr_implementation) < 0)
                    return gs_error_Fatal;
                if (pl_init_job(pjli) < 0)
                    return gs_error_Fatal;

                code = 0;
                new_job = true;
            }
        }
    }
    pl_cursor_close(&r);
    return 0;
#undef curr_implementation
#undef desired_implementation
}

int
pl_main_delete_instance(pl_main_instance_t *minst)
{
    gs_memory_t *mem = minst->memory;
    /* Dnit PDLs */
    int index;
    pl_interp_implementation_t **impl = minst->implementations;

    /* dnit interps */
    for (index = 0; impl[index] != 0; ++index) {
        if (pl_deallocate_interp_instance(impl[index]) < 0) {
            return -1;
        }
    }

    gs_free_object(mem, impl, "pl_main_languages_delete_instance()");

    /* close and deallocate the device */
    if (minst->device) {
        gs_closedevice(minst->device);
        gs_unregister_root(minst->device->memory, &device_root,
                           "pl_main_languages_delete_instance");
        gx_device_retain(minst->device, false);
        minst->device = NULL;
    }

#ifdef PL_LEAK_CHECK
    gs_memory_chunk_dump_memory(mem);
#endif
    gs_malloc_release(gs_memory_chunk_unwrap(mem));

    return 0;
}

int
pl_to_exit(gs_memory_t *mem)
{
    pl_main_instance_t *minst = mem->gs_lib_ctx->top_of_system;
    /* Deselect last-selected device */
    if (minst->curr_implementation
        && pl_remove_device(minst->curr_implementation) < 0) {
        return -1;
    }

    gs_c_param_list_release(&minst->params);
    arg_finit(&minst->args);
    gp_exit(0, 0);
    return 0;
}

static int                             /* 0 ok, else -1 error */
pl_main_languages_init(gs_memory_t * mem,        /* deallocator for devices */
                      pl_main_instance_t * minst         /* instance for pre/post print */
    )
{
    int index;
    int count;
    int sz;
    pl_interp_implementation_t **impls;

    for (count = 0; pdl_implementations[count] != 0; ++count)
        ;

    /* add one so we can zero terminate the table */
    sz = sizeof(pl_interp_implementation_t *) * (count + 1);

    impls = (pl_interp_implementation_t **)gs_alloc_bytes_immovable(mem, sz, "pl_main_languages_init");

    if (impls == NULL)
        goto pmui_err;

    minst->implementations = impls;
    minst->curr_implementation = minst->desired_implementation = NULL;
    memset(impls, 0, sz);
    
    /* Create & init PDL all instances. Could do this lazily to save memory, */
    /* but for now it's simpler to just create all instances up front. */
    for (index = 0; index < count; ++index) {
        int code;
        impls[index] = (pl_interp_implementation_t *)
            gs_alloc_bytes_immovable(mem,
                                     sizeof(pl_interp_implementation_t),
                                     "pl_main_languages_init interp");
        
        if (impls[index] == NULL)
            goto pmui_err;
            
        *impls[index] = *pdl_implementations[index];
        
        /* Whatever instance we allocate here will become the current
         * instance during initialisation; this allows init files to be
         * successfully read etc. */
        code = pl_allocate_interp_instance(impls[index], mem);

        if (code < 0) {
            errprintf(mem, "Unable to create %s interpreter.\n",
                        pl_characteristics(impls[index])->
                        language);
            goto pmui_err;
        }

    }
    
    return 0;

 pmui_err:
    return -1;
}

pl_main_instance_t *
pl_main_get_instance(const gs_memory_t *mem)
{
    return mem->gs_lib_ctx->top_of_system;
}

char *
pl_main_get_pcl_personality(const gs_memory_t *mem)
{
    return pl_main_get_instance(mem)->pcl_personality;
}

/* ------- Functions related to pl_main_instance_t ------ */

/* Initialize the instance parameters. */
pl_main_instance_t *
pl_main_alloc_instance(gs_memory_t * mem)
{
    pl_main_instance_t *minst;
    if (mem == NULL)
        return NULL;

    minst = (pl_main_instance_t *)gs_alloc_bytes_immovable(mem,
                                                           sizeof(pl_main_instance_t),
                                                           "pl_main_instance");
    if (minst == NULL)
        return 0;

    memset(minst, 0, sizeof(*minst));

    minst->memory = mem;
    
    minst->pjl_from_args = false;
    minst->error_report = -1;
    minst->pause = true;
    minst->device = 0;
    minst->implementation = 0;
    minst->base_time[0] = 0;
    minst->base_time[1] = 0;
    minst->interpolate = false;
    minst->nocache = false;
    minst->page_set_on_command_line = false;
    minst->res_set_on_command_line = false;
    minst->high_level_device = false;
    minst->saved_pages_test_mode = false;
    minst->scanconverter = GS_SCANCONVERTER_DEFAULT;
    minst->piccdir = NULL;
    minst->pdefault_gray_icc = NULL;
    minst->pdefault_rgb_icc = NULL;
    minst->pdefault_cmyk_icc = NULL;
    strncpy(&minst->pcl_personality[0], "PCL",
            sizeof(minst->pcl_personality) - 1);
    mem->gs_lib_ctx->top_of_system = minst;
    return minst;
}

/* -------- Command-line processing ------ */

/* Create a default device if not already defined. */
static int
pl_top_create_device(pl_main_instance_t * pti, int index, bool is_default)
{
    int code = 0;

    if (!is_default || !pti->device) {
        const gx_device *dev;
        /* We assume that nobody else changes pti->device,
           and this function is called from this module only.
           Due to that device_root is always consistent with pti->device,
           and it is regisrtered if and only if pti->device != NULL.
         */
        if (pti->device != NULL)
            pti->device = NULL;

        if (index == -1) {
            dev = gs_getdefaultlibdevice(pti->memory);
        }
        else {
            const gx_device **list;
            gs_lib_device_list((const gx_device * const **)&list, NULL);
            dev = list[index];
        }
        code = gs_copydevice(&pti->device, dev, pti->memory);

        if (code < 0)
            return code;

        if (pti->device == NULL)
            return gs_error_VMerror;

        gs_register_struct_root(pti->memory, &device_root,
                                (void **)&pti->device,
                                "pl_top_create_device");
        
        /* NB this needs a better solution */
        if (!strcmp(gs_devicename(pti->device), "pdfwrite") ||
            !strcmp(gs_devicename(pti->device), "ps2write"))
            pti->high_level_device = true;

            /* If the display device is selected (default), set up the callback.  NB Move me. */
        if (strcmp(gs_devicename(pti->device), "display") == 0) {
            gx_device_display *ddev;
        
            if (!pti->disp) {
                code = -1;
            } else {
                ddev = (gx_device_display *) pti->device;
                ddev->callback = (display_callback *) pti->disp;

            }
        }
    }
    return code;
}

/* Process the options on the command line. */
static FILE *
pl_main_arg_fopen(const char *fname, void *ignore_data)
{
    return gp_fopen(fname, "r");
}

static void
set_debug_flags(const char *arg, char *flags)
{
    byte value = (*arg == '-' ? (++arg, 0) : 0xff);

    while (*arg)
        flags[*arg++ & 127] = value;
}

/*
 * scan floats delimited by spaces, tabs and/or 'x'.  Return the
 * number of arguments parsed which will be less than or equal to the
 * number of arguments requested in the parameter arg_count.
 */
static int
parse_floats(gs_memory_t * mem, uint arg_count, char *arg, float *f)
{
    int float_index = 0;
    char *tok, *l = NULL;
    /* copy the input because strtok() steps on the string */
    char *s = arg_copy(arg, mem);
    if (s == NULL)
        return -1;
    
    /* allow 'x', tab or spaces to delimit arguments */
    tok = gs_strtok(s, " \tx", &l);
    while (tok != NULL && float_index < arg_count) {
        f[float_index++] = atof(tok);
        tok = gs_strtok(NULL, " \tx", &l);
    }

    gs_free_object(mem, s, "parse_floats()");

    /* return the number of args processed */
    return float_index;
}

static int check_for_special_int(pl_main_instance_t * pmi, const char *arg, int b)
{
    if (!strncmp(arg, "BATCH", 5))
        return (b == 0) ? 0 : gs_note_error(gs_error_rangecheck);
    if (!strncmp(arg, "NOPAUSE", 6)) {
        pmi->pause = !b;
        return 0;
    }
    if (!strncmp(arg, "DOINTERPOLATE", 13)) {
        pmi->interpolate = !!b;
        return 0;
    }
    if (!strncmp(arg, "NOCACHE", 7)) {
        pmi->nocache = !!b;
        return 0;
    }
    if (!strncmp(arg, "SCANCONVERTERTYPE", 17)) {
        pmi->scanconverter = b;
        return 0;
    }
    return 1;
}

static int check_for_special_float(pl_main_instance_t * pmi, const char *arg, float f)
{
    if (!strncmp(arg, "BATCH", 5) ||
        !strncmp(arg, "NOPAUSE", 6) ||
        !strncmp(arg, "DOINTERPOLATE", 13) ||
        !strncmp(arg, "NOCACHE", 7) ||
        !strncmp(arg, "SCANCONVERTERTYPE", 17)) {
        return gs_note_error(gs_error_rangecheck);
    }
    return 1;
}

static int check_for_special_str(pl_main_instance_t * pmi, const char *arg, gs_param_string *f)
{
    if (!strncmp(arg, "BATCH", 5) ||
        !strncmp(arg, "NOPAUSE", 6) ||
        !strncmp(arg, "DOINTERPOLATE", 13) ||
        !strncmp(arg, "NOCACHE", 7) ||
        !strncmp(arg, "SCANCONVERTERTYPE", 17)) {
        return gs_note_error(gs_error_rangecheck);
    }
    return 1;
}

#define arg_heap_copy(str) arg_copy(str, pmi->memory)
static int
pl_main_process_options(pl_main_instance_t * pmi, arg_list * pal,
                        pl_interp_implementation_t * pjli)
{
    int code = 0;
    bool help = false;
    char *arg;
    gs_c_param_list *params = &pmi->params;
    
    gs_c_param_list_write_more(params);
    while ((code = arg_next(pal, (const char **)&arg, pmi->memory)) > 0 && *arg == '-') {
        if (arg[1] == '\0') /* not an option, stdin */
            break;
        arg += 2;
        switch (arg[-1]) {
            case '-':
                if (strcmp(arg, "debug") == 0) {
                    gs_debug_flags_list(pmi->memory);
                    break;
                } else if (strncmp(arg, "debug=", 6) == 0) {
                    gs_debug_flags_parse(pmi->memory, arg + 6);
                    break;
                } else if (strncmp(arg, "saved-pages=", 12) == 0) {
                    gx_device *pdev = pmi->device;
                    gx_device_printer *ppdev = (gx_device_printer *)pdev;

                    /* open the device if not yet open */
                    if (pdev->is_open == 0 && (code = gs_opendevice(pdev)) < 0) {
                        return code;
                    }
                    if (dev_proc(pdev, dev_spec_op)(pdev, gxdso_supports_saved_pages, NULL, 0) == 0) {
                        errprintf(pmi->memory, "   --saved-pages not supported by the '%s' device.\n",
                                  pdev->dname);
                        return -1;
                    }
                    code = gx_saved_pages_param_process(ppdev, (byte *)arg+12, strlen(arg+12));
                    if (code > 0) {
                        /* erase the page */
                        gx_color_index color;
                        gx_color_value rgb_white[3] = { 65535, 65535, 65535 };
                        gx_color_value cmyk_white[4] = { 0, 0, 0, 0 };

                        if (pdev->color_info.polarity == GX_CINFO_POLARITY_ADDITIVE)
                            color = dev_proc(pdev, map_rgb_color)(pdev, rgb_white);
                        else
                            color = dev_proc(pdev, map_cmyk_color)(pdev, cmyk_white);
                        code = dev_proc(pdev, fill_rectangle)(pdev, 0, 0, pdev->width, pdev->height, color);
                    }
                    if (code < 0)
                        return code;
                    break;
                /* The following code is only to allow regression testing of saved-pages */
                } else if (strncmp(arg, "saved-pages-test", 16) == 0) {
                    gx_device *pdev = pmi->device;

                    if ((code = gs_opendevice(pdev)) < 0)
                        return code;

                    if (dev_proc(pdev, dev_spec_op)(pdev, gxdso_supports_saved_pages, NULL, 0) == 0) {
                        errprintf(pmi->memory, "   --saved-pages-test not supported by the '%s' device.\n",
                                  pdev->dname);
                        break;			/* just ignore it */
                    }
                    code = gx_saved_pages_param_process((gx_device_printer *)pdev, (byte *)"begin", 5);
                    if (code > 0) {
                        /* erase the page */
                        gx_color_index color;
                        gx_color_value rgb_white[3] = { 65535, 65535, 65535 };
                        gx_color_value cmyk_white[4] = { 0, 0, 0, 0 };

                        if (pdev->color_info.polarity == GX_CINFO_POLARITY_ADDITIVE)
                            color = dev_proc(pdev, map_rgb_color)(pdev, rgb_white);
                        else
                            color = dev_proc(pdev, map_cmyk_color)(pdev, cmyk_white);
                        code = dev_proc(pdev, fill_rectangle)(pdev, 0, 0, pdev->width, pdev->height, color);
                    }
                    if (code < 0)
                        return code;
                    pmi->saved_pages_test_mode = true;
                    break;
                }
                /* FALLTHROUGH */
            default:
                dmprintf1(pmi->memory, "Unrecognized switch: %s\n", arg);
                return -1;
            case '\0':
                /* read from stdin - must be last arg */
                continue;
            case 'd':
            case 'D':
                {
                    /* We're setting a device parameter to a non-string value. */
                    char *eqp = strchr(arg, '=');
                    const char *value;
                    int vi;
                    float vf;
                    bool bval = true;
                    char buffer[128];

                    if (eqp || (eqp = strchr(arg, '#')))
                        value = eqp + 1;
                    else {
                        /* -dDefaultBooleanIs_TRUE */
                        code = check_for_special_int(pmi, arg, (int)bval);
                        if (code == 1)
                            code =
                                param_write_bool((gs_param_list *) params,
                                                 arg_heap_copy(arg), &bval);
                        continue;
                    }

                    if (value && value[0] == '/') {
                        gs_param_string str;

                        strncpy(buffer, arg, eqp - arg);
                        buffer[eqp - arg] = '\0';
                        param_string_from_transient_string(str, value + 1);
                        code = check_for_special_str(pmi, arg, &str);
                        if (code == 1)
                            code = param_write_name((gs_param_list *) params,
                                                    arg_heap_copy(buffer), &str);
                        continue;
                    }
                    /* Search for a non-decimal 'radix' number */
                    else if (strchr(value, '#')) {
                        int base, number = 0;
                        char *val = strchr(value, '#');

                        *val++ = 0x00;
                        sscanf(value, "%d", &base);
                        if (base < 2 || base > 36) {
                            dmprintf1(pmi->memory, "Value out of range %s",
                                      value);
                            return -1;
                        }
                        while (*val) {
                            if (*val >= '0' && *val <= '9') {
                                number = number * base + (*val - '0');
                            } else {
                                if (*val >= 'A' && *val <= 'Z') {
                                    number = number * base + (*val - 'A');
                                } else {
                                    if (*val >= 'a' && *val <= 'z') {
                                        number = number * base + (*val - 'a');
                                    } else {
                                        dmprintf1(pmi->memory,
                                                  "Value out of range %s",
                                                  val);
                                        return -1;
                                    }
                                }
                            }
                            val++;
                        }
                        strncpy(buffer, arg, eqp - arg);
                        buffer[eqp - arg] = '\0';
                        code = check_for_special_int(pmi, arg, number);
                        if (code == 1)
                            code =
                                param_write_int((gs_param_list *) params,
                                                arg_heap_copy(buffer), &number);
                    } else if ((!strchr(value, '.')) &&
                               /* search for an int (no decimal), if fail try a float */
                               (sscanf(value, "%d", &vi) == 1)) {
                        /* Here we have an int -- check for a scaling suffix */
                        char suffix = eqp[strlen(eqp) - 1];

                        switch (suffix) {
                            case 'k':
                            case 'K':
                                vi *= 1024;
                                break;
                            case 'm':
                            case 'M':
                                vi *= 1024 * 1024;
                                break;
                            case 'g':
                            case 'G':
                                /* caveat emptor: more than 2g will overflow */
                                /* and really should produce a 'real', so don't do this */
                                vi *= 1024 * 1024 * 1024;
                                break;
                            default:
                                break;  /* not a valid suffix or last char was digit */
                        }
                        /* create a null terminated string for the key */
                        strncpy(buffer, arg, eqp - arg);
                        buffer[eqp - arg] = '\0';
                        code = check_for_special_int(pmi, arg, vi);
                        if (code == 1)
                            code =
                                param_write_int((gs_param_list *) params,
                                                arg_heap_copy(buffer), &vi);
                    } else if (sscanf(value, "%f", &vf) == 1) {
                        /* create a null terminated string.  NB duplicated code. */
                        strncpy(buffer, arg, eqp - arg);
                        buffer[eqp - arg] = '\0';
                        code = check_for_special_float(pmi, arg, vf);
                        if (code == 1)
                            code =
                                param_write_float((gs_param_list *) params,
                                                  arg_heap_copy(buffer), &vf);
                    } else if (!strcmp(value, "true")) {
                        /* bval = true; */
                        strncpy(buffer, arg, eqp - arg);
                        buffer[eqp - arg] = '\0';
                        code = check_for_special_int(pmi, arg, (int)bval);
                        if (code == 1)
                            code =
                                param_write_bool((gs_param_list *) params,
                                                 arg_heap_copy(buffer), &bval);
                    } else if (!strcmp(value, "false")) {
                        bval = false;
                        strncpy(buffer, arg, eqp - arg);
                        buffer[eqp - arg] = '\0';
                        code = check_for_special_int(pmi, arg, (int)bval);
                        if (code == 1)
                            code =
                                param_write_bool((gs_param_list *) params,
                                                 arg_heap_copy(buffer), &bval);
                    } else {
                        dmprintf(pmi->memory,
                                 "Usage for -d is -d<option>=[<integer>|<float>|true|false]\n");
                        continue;
                    }
                }
                break;
            case 'E':
                if (*arg == 0)
                    gs_debug['#'] = 1;
                else
                    sscanf(arg, "%d", &pmi->error_report);
                break;
            case 'g':
                {
                    int geom[2];
                    gs_param_int_array ia;

                    if (sscanf(arg, "%ux%u", &geom[0], &geom[1]) != 2) {
                        dmprintf(pmi->memory,
                                 "-g must be followed by <width>x<height>\n");
                        return -1;
                    }
                    ia.data = geom;
                    ia.size = 2;
                    ia.persistent = false;
                    code =
                        param_write_int_array((gs_param_list *) params,
                                              "HWSize", &ia);
                    pmi->page_set_on_command_line = true;
                }
                break;
            case 'H':
                {
                    float hwmarg[4];
                    gs_param_float_array fa;
                    uint sz = countof(hwmarg);
                    uint parsed = parse_floats(pmi->memory, sz, arg, hwmarg);
                    if (parsed != sz) {
                        dmprintf(pmi->memory,
                                 "-H must be followed by <left>x<bottom>x<right>x<top>\n");
                        return -1;
                    }
                    fa.data = hwmarg;
                    fa.size = parsed;
                    fa.persistent = false;
                    code =
                        param_write_float_array((gs_param_list *) params,
                                              ".HWMargins", &fa);
                }
                break;
            case 'm':
                {
                    float marg[2];
                    gs_param_float_array fa;
                    uint sz = countof(marg);
                    uint parsed = parse_floats(pmi->memory, sz, arg, marg);
                    if (parsed != sz) {
                        dmprintf(pmi->memory,
                                 "-m must be followed by <left>x<bottom>\n");
                        return -1;
                    }
                    fa.data = marg;
                    fa.size = parsed;
                    fa.persistent = false;
                    code =
                        param_write_float_array((gs_param_list *) params,
                                                "Margins", &fa);

                }
                break;
            case 'h':
                help = true;
                goto out;
                /* job control line follows - PJL */
            case 'j':
            case 'J':
                /* set up the read cursor and send it to the pjl parser */
                {
                    stream_cursor_read cursor;
                    /* PJL lines have max length of 80 character + null terminator */
                    byte buf[512];
                    /* length of arg + newline (expected by PJL parser) + null */
                    int buf_len = strlen(arg) + 2;
                    if ((buf_len) > sizeof(buf)) {
                        dmprintf(pmi->memory, "pjl sequence too long\n");
                        return -1;
                    }

                    /* replace ";" with newline for the PJL parser and
                       concatenate NULL. */
                    {
                        int i;

                        for (i = 0; i < buf_len - 2; i++) {
                            if (arg[i] == ';')
                                buf[i] = '\n';
                            else
                                buf[i] = arg[i];
                        }
                        buf[i] = '\n';
                        i++;
                        buf[i] = '\0';
                    }
                    /* starting pos for pointer is always one position back */
                    cursor.ptr = buf - 1;
                    /* set the end of data pointer */
                    cursor.limit = cursor.ptr + buf_len;
                    /* process the pjl */
                    code = pl_process(pjli, &cursor);
                    pmi->pjl_from_args = true;
                    /* we expect the language to exit properly otherwise
                       there was some sort of problem */
                    if (code != e_ExitLanguage)
                        return (code == 0 ? -1 : code);
                    else
                        code = 0;
                }
                break;
            case 'K':          /* max memory in K */
                {
                    int maxk;
                    gs_malloc_memory_t *rawheap =
                        (gs_malloc_memory_t *) gs_memory_chunk_target(pmi->
                                                                      memory)->
                        non_gc_memory;
                    if (sscanf(arg, "%d", &maxk) != 1) {
                        dmprintf(pmi->memory,
                                 "-K must be followed by a number\n");
                        return -1;
                    }
                    rawheap->limit = (long)maxk << 10;
                }
                break;
            case 'o':
                {
                    const char *adef;
                    gs_param_string str;

                    if (arg[0] == 0) {
                        code = arg_next(pal, (const char **)&adef, pmi->memory);
                        if (code < 0)
                            break;
                    } else
                        adef = arg;
                    param_string_from_transient_string(str, adef);
                    code =
                        param_write_string((gs_param_list *) params,
                                           "OutputFile", &str);
                    pmi->pause = false;
                    break;
                }
            case 'L':          /* language */
                {
                    int index;
                    pl_interp_implementation_t **impls = pmi->implementations;

                    /* 
                     * the 0th entry always holds the PJL
                     * implementation, which is never explicitly
                     * selected.
                     */
                    for (index = 1; impls[index] != 0; ++index)
                        if (!strcmp(arg,
                                    pl_characteristics(impls[index])->
                                    language))
                            break;
                    if (impls[index] != 0)
                        pmi->implementation = impls[index];
                    else {
                        dmprintf(pmi->memory,
                                 "Choose language in -L<language> from: ");
                        for (index = 1; impls[index] != 0; ++index)
                            dmprintf1(pmi->memory, "%s ",
                                      pl_characteristics(impls[index])->
                                      language);
                        dmprintf(pmi->memory, "\n");
                        return -1;
                    }
                    break;
                }
            case 'l':          /* personality */
                {
                    if (!strcmp(arg, "RTL") || !strcmp(arg, "PCL5E") ||
                        !strcmp(arg, "PCL5C"))
                        strcpy(pmi->pcl_personality, arg);
                    else
                        dmprintf(pmi->memory,
                                 "PCL personality must be RTL, PCL5E or PCL5C\n");
                }
                break;
            case 'r':
                {
                    float res[2];
                    gs_param_float_array fa;
                    uint sz = countof(res);
                    uint parsed = parse_floats(pmi->memory, sz, arg, res);
                    switch (parsed) {
                        default:
                            dmprintf(pmi->memory,
                                     "-r must be followed by <res> or <xres>x<yres>\n");
                            return -1;
                        case 1:        /* -r<res> */
                            res[1] = res[0];
                        case 2:        /* -r<xres>x<yres> */
                            ;
                    }
                    fa.data = res;
                    fa.size = sz;
                    fa.persistent = false;
                    code =
                        param_write_float_array((gs_param_list *) params,
                                                "HWResolution", &fa);
                    if (code == 0)
                        pmi->res_set_on_command_line = true;
                }
                break;
            case 's':
            case 'S':
                {               /* We're setting a device or user parameter to a string. */
                    char *eqp;
                    const char *value;
                    gs_param_string str;

                    eqp = strchr(arg, '=');
                    if (!(eqp || (eqp = strchr(arg, '#')))) {
                        dmprintf(pmi->memory,
                                 "Usage for -s is -s<option>=<string>\n");
                        return -1;
                    }
                    value = eqp + 1;
                    if (!strncmp(arg, "DEVICE", 6)) {
                        int code = pl_top_create_device(pmi,
                                                        get_device_index(pmi->
                                                                         memory,
                                                                         value),
                                                        false);

                        if (code < 0)
                            return code;
                        /* check for icc settings */
                    } else
                        if (!strncmp
                            (arg, "DefaultGrayProfile",
                             strlen("DefaultGrayProfile"))) {
                        pmi->pdefault_gray_icc = arg_heap_copy(value);
                    } else
                        if (!strncmp
                            (arg, "DefaultRGBProfile",
                             strlen("DefaultRGBProfile"))) {
                        pmi->pdefault_rgb_icc = arg_heap_copy(value);
                    } else
                        if (!strncmp
                            (arg, "DefaultCMYKProfile",
                             strlen("DefaultCMYKProfile"))) {
                        pmi->pdefault_cmyk_icc = arg_heap_copy(value);
                    } else
                        if (!strncmp
                            (arg, "ICCProfileDir", strlen("ICCProfileDir"))) {
                        pmi->piccdir = arg_heap_copy(value);
                    } else {
                        char buffer[128];

                        strncpy(buffer, arg, eqp - arg);
                        buffer[eqp - arg] = '\0';
                        param_string_from_transient_string(str, value);
                        code =
                            param_write_string((gs_param_list *) params,
                                               buffer, &str);
                    }
                }
                break;
            case 'Z':
                set_debug_flags(arg, gs_debug);
                break;
        }
    }
  out:if (help) {
        arg_finit(pal);
        gs_c_param_list_release(params);
        return -1;
    }
    gs_c_param_list_read(params);
    code = pl_top_create_device(pmi, -1, true); /* create default device if needed */
    if (code < 0)
        return code;

    code = gs_putdeviceparams(pmi->device, (gs_param_list *) params);
    if (code < 0)
        return code;

    code = gs_opendevice(pmi->device);
    if (code < 0)
        return code;
    
    /* No file names to process.*/
    if (!arg)
        return 0;

    code = arg_push_string(pal, arg, true /* parsed */);
    if (code < 0)
        return code;
    
    while (arg_next(pal, (const char **)&arg, pmi->memory) > 0) {
        code = pl_main_run_file(pmi, arg);
        if (code < 0)
            return code;
    }
    return 0;
}

#define PJL_UEL "\033%-12345X"

/* Find default language implementation */
static pl_interp_implementation_t *
pl_auto_sense(pl_main_instance_t *minst, const char *name,  int buffer_length)
{
    /* Lookup this string in the auto sense field for each implementation */
    pl_interp_implementation_t **impls = minst->implementations;
    pl_interp_implementation_t **impl;
    size_t uel_len = strlen(PJL_UEL);

    /* first check for a UEL */
    if (buffer_length >= uel_len) {
        if (!memcmp(name, PJL_UEL, uel_len))
            return impls[0];
    }
    
    for (impl = impls; *impl != 0; ++impl) {
        int auto_string_len = strlen(pl_characteristics(*impl)->auto_sense_string);
        const char *sense   = pl_characteristics(*impl)->auto_sense_string;
        if (auto_string_len != 0 && buffer_length >= auto_string_len)
            if (!memcmp(sense, name, auto_string_len))
                return *impl;
    }
    /* Defaults to first language PJL is first in the table */
    return impls[1];
}

/* either the (1) implementation has been selected on the command line or
   (2) it has been selected in PJL or (3) we need to auto sense. */
static pl_interp_implementation_t *
pl_select_implementation(pl_interp_implementation_t * pjli,
                         pl_main_instance_t * pmi, pl_top_cursor_t r)
{
    /* Determine language of file to interpret. We're making the incorrect */
    /* assumption that any file only contains jobs in one PDL. The correct */
    /* way to implement this would be to have a language auto-detector. */
    pl_interp_implementation_t *impl;

    if (pmi->implementation)
        return pmi->implementation;     /* was specified as cmd opt */
    /* select implementation */
    if ((impl = pl_pjl_select(pmi, pjli)) != 0)
        return impl;
    /* lookup string in name field for each implementation */
    return pl_auto_sense(pmi, (const char *)r.cursor.ptr + 1,
                         (r.cursor.limit - r.cursor.ptr));
}

/* Find default language implementation */
static pl_interp_implementation_t *
pl_pjl_select(pl_main_instance_t * pmi, pl_interp_implementation_t * pjli)
{
    pjl_envvar_t *language;
    pl_interp_implementation_t **impls = pmi->implementations;
    pl_interp_implementation_t **impl;

    language = pjl_proc_get_envvar(pjli, "language");
    for (impl = impls; *impl != 0; ++impl) {
        if (!strcmp(pl_characteristics(*impl)->language, language))
            return *impl;
    }
    return 0;
}

/* Print memory and time usage. */
void
pl_print_usage(const pl_main_instance_t * pti, const char *msg)
{
    long utime[2];
    gx_device *pdev = pti->device;
    
    gp_get_realtime(utime);
    dmprintf3(pti->memory, "%% %s time = %g, pages = %ld\n",
              msg, utime[0] - pti->base_time[0] +
              (utime[1] - pti->base_time[1]) / 1000000000.0, pdev->PageCount);
}

/* Log a string to console, optionally wait for input */
static void
pl_log_string(const gs_memory_t * mem, const char *str, int wait_for_key)
{
    errwrite(mem, str, strlen(str));
    if (wait_for_key)
        (void)fgetc(mem->gs_lib_ctx->fstdin);
}

pl_interp_implementation_t *
pl_main_get_pcl_instance(const gs_memory_t *mem)
{
    return pl_main_get_instance(mem)->implementations[1];
}

pl_interp_implementation_t *
pl_main_get_pjl_instance(const gs_memory_t *mem)
{
    return pl_main_get_instance(mem)->implementations[0];
}

bool pl_main_get_interpolate(const gs_memory_t *mem)
{
    return pl_main_get_instance(mem)->interpolate;
}

bool pl_main_get_nocache(const gs_memory_t *mem)
{
    return pl_main_get_instance(mem)->nocache;
}

bool pl_main_get_page_set_on_command_line(const gs_memory_t *mem)
{
    return pl_main_get_instance(mem)->page_set_on_command_line;
}

bool pl_main_get_res_set_on_command_line(const gs_memory_t *mem)
{
    return pl_main_get_instance(mem)->res_set_on_command_line;
}

bool pl_main_get_high_level_device(const gs_memory_t *mem)
{
    return pl_main_get_instance(mem)->high_level_device;
}

bool pl_main_get_scanconverter(const gs_memory_t *mem)
{
    return pl_main_get_instance(mem)->scanconverter;
}

int
pl_set_icc_params(const gs_memory_t *mem, gs_gstate *pgs)
{
    pl_main_instance_t *minst = pl_main_get_instance(mem);
    gs_param_string p;
    int code = 0;
    
    if (minst->pdefault_gray_icc) {
        param_string_from_transient_string(p, minst->pdefault_gray_icc);
        code = gs_setdefaultgrayicc(pgs, &p);
        if (code < 0)
            return gs_throw_code(gs_error_Fatal);
    }

    if (minst->pdefault_rgb_icc) {
        param_string_from_transient_string(p, minst->pdefault_rgb_icc);
        code = gs_setdefaultrgbicc(pgs, &p);
        if (code < 0)
            return gs_throw_code(gs_error_Fatal);
    }

    if (minst->piccdir) {
        param_string_from_transient_string(p, minst->piccdir);
        code = gs_seticcdirectory(pgs, &p);
        if (code < 0)
            return gs_throw_code(gs_error_Fatal);
    }
    return code;
}

int
pl_finish_page(pl_main_instance_t * pmi, gs_gstate * pgs, int num_copies, int flush)
{
    int code        = 0;
    gx_device *pdev = pmi->device;

    /* output the page */
    if (gs_debug_c(':'))
        pl_print_usage(pmi, "parse done :");
    code = gs_output_page(pgs, num_copies, flush);
    if (code < 0)
        return code;

    if (pmi->pause) {
        char strbuf[256];

        gs_sprintf(strbuf, "End of page %d, press <enter> to continue.\n",
                pdev->PageCount);
        pl_log_string(pmi->memory, strbuf, 1);
    } else if (gs_debug_c(':'))
        pl_print_usage(pmi, "render done :");
    return 0;
}
