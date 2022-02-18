
/* regress_pro_window.cpp
 *
 * Copyright (C) 2005-2011 Francesco Abbate
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#include <gsl/gsl_vector.h>

#include "regress_pro_window.h"
#include "str_cpp.h"
#include "error-messages.h"
#include "fit-engine.h"
#include "multi-fit-engine.h"
#include "lmfit-multi.h"
#include "fit-params.h"
#include "spectra.h"
#include "nlopt_fit.h"
#include "str.h"
#include "str-util.h"
#include "dispers-library.h"
#include "disp_fit_manager.h"
#include "fit_window.h"
#include "disp_fit_window.h"
#include "interactive_fit.h"
#include "recipe_window.h"
#include "filmstack_window.h"
#include "dataset_window.h"
#include "batch_window.h"
#include "lexer.h"
#include "gsl_cpp.h"

static float timeval_subtract(struct timeval *x, struct timeval *y);
static fit_engine *prepare_fit_engine(stack_t *stack, fit_parameters *parameters, const fit_config *config, str_ptr *error_msg);

// Map
FXDEFMAP(regress_pro_window) regress_pro_window_map[]= {
    FXMAPFUNC(SEL_UPDATE,  0, regress_pro_window::onUpdate),
    FXMAPFUNC(SEL_COMMAND, regress_pro_window::ID_ABOUT,  regress_pro_window::onCmdAbout),
    FXMAPFUNC(SEL_COMMAND, regress_pro_window::ID_DATASET_EDIT, regress_pro_window::onCmdDatasetEdit),
    FXMAPFUNC(SEL_COMMAND, regress_pro_window::ID_RECIPE_SAVE, regress_pro_window::onCmdRecipeSaveAs),
    FXMAPFUNC(SEL_COMMAND, regress_pro_window::ID_RECIPE_LOAD, regress_pro_window::onCmdRecipeLoad),
    FXMAPFUNC(SEL_COMMAND, regress_pro_window::ID_LOAD_SPECTRA, regress_pro_window::onCmdLoadSpectra),
    FXMAPFUNC(SEL_COMMAND, regress_pro_window::ID_DISP_OPTIM, regress_pro_window::onCmdDispersOptim),
    FXMAPFUNC(SEL_COMMAND, regress_pro_window::ID_RUN_FIT, regress_pro_window::onCmdRunFit),
    FXMAPFUNC(SEL_COMMAND, regress_pro_window::ID_INTERACTIVE_FIT, regress_pro_window::onCmdInteractiveFit),
    FXMAPFUNC(SEL_COMMAND, regress_pro_window::ID_RUN_MULTI_FIT, regress_pro_window::onCmdRunMultiFit),
    FXMAPFUNC(SEL_COMMAND, regress_pro_window::ID_RUN_BATCH, regress_pro_window::onCmdBatchWindow),
    FXMAPFUNC(SEL_COMMAND, regress_pro_window::ID_STACK_CHANGE, regress_pro_window::onCmdStackChange),
    FXMAPFUNC(SEL_COMMAND, regress_pro_window::ID_STACK_SHIFT, regress_pro_window::onCmdStackShift),
    FXMAPFUNC(SEL_COMMAND, regress_pro_window::ID_RESULT_STACK, regress_pro_window::onCmdResultStack),
};


// Object implementation
FXIMPLEMENT(regress_pro_window,FXMainWindow,regress_pro_window_map,ARRAYNUMBER(regress_pro_window_map));


const FXchar regress_pro_window::patterns_spectr[] =
    "Fit Strategy (*.dat,*.txt,*.csv)"
    "\nAll Files (*)";
const FXchar regress_pro_window::patterns_recipe[] =
    "Fit Recipe (*.rcp)"
    "\nAll Files (*)";

const FXHiliteStyle regress_pro_window::tstyles[] = {
    {FXRGB(255,255,255), FXRGB(255,0,0), 0, 0, 0, 0, 0, 0}
};



// Make some windows
regress_pro_window::regress_pro_window(regress_pro* a)
    : FXMainWindow(a,"Regress Pro",nullptr,&a->appicon,DECOR_ALL,20,20,780,580),
      spectrum(nullptr), recipeFilename("untitled"),
      result_filmstack_window(nullptr), m_disp_fit_window(nullptr), my_batch_window(nullptr),
      m_enlarged_window(false), m_result_stack_match(true)
{
    // Menubar
    menubar=new FXMenuBar(this, LAYOUT_SIDE_TOP|LAYOUT_FILL_X);
    statusbar=new FXStatusBar(this,LAYOUT_SIDE_BOTTOM|LAYOUT_FILL_X);

    // Script menu
    filemenu=new FXMenuPane(this);
    new FXMenuCommand(filemenu,"&Save",nullptr,this,ID_RECIPE_SAVE);
    new FXMenuCommand(filemenu,"&Load",nullptr,this,ID_RECIPE_LOAD);
    new FXMenuCommand(filemenu,"&Quit\tCtl-Q",nullptr,getApp(),FXApp::ID_QUIT);
    new FXMenuTitle(menubar,"&Recipe",nullptr,filemenu);

    // Edit menu
    editmenu = new FXMenuPane(this);
    new FXMenuCommand(editmenu, "Dataset", nullptr, this, ID_DATASET_EDIT);
    new FXMenuTitle(menubar, "&Edit", nullptr, editmenu);

    // Script menu
    spectrmenu = new FXMenuPane(this);
    new FXMenuCommand(spectrmenu,"&Load Spectra",nullptr,this,ID_LOAD_SPECTRA);
    new FXMenuTitle(menubar,"S&pectra",nullptr,spectrmenu);

    // Dispersion menu
    dispmenu = new FXMenuPane(this);
    new FXMenuCommand(dispmenu, "Dispersion Optimize",nullptr,this,ID_DISP_OPTIM);
    new FXMenuTitle(menubar,"&Dispersion",nullptr,dispmenu);

    // Fit menu
    fitmenu = new FXMenuPane(this);
    new FXMenuCommand(fitmenu, "&Run Fitting",nullptr,this,ID_RUN_FIT);
    new FXMenuCommand(fitmenu, "&Interactive Fit",nullptr,this,ID_INTERACTIVE_FIT);
    new FXMenuCommand(fitmenu, "Run &Multiple Fit",nullptr,this,ID_RUN_MULTI_FIT);
    new FXMenuCommand(fitmenu, "&Batch Window",nullptr,this,ID_RUN_BATCH);
    new FXMenuCommand(fitmenu, "Edit Result Stack",nullptr,this,ID_RESULT_STACK);
    new FXMenuTitle(menubar,"Fittin&g",nullptr,fitmenu);

    helpmenu = new FXMenuPane(this);
    new FXMenuCommand(helpmenu, "&About", nullptr, this, ID_ABOUT);
    new FXMenuTitle(menubar, "&Help", nullptr, helpmenu, LAYOUT_RIGHT);

    // Container
    FXVerticalFrame *cont = new FXVerticalFrame(this,LAYOUT_FILL_X|LAYOUT_FILL_Y);

    FXSpring *rcpspring = new FXSpring(cont, LAYOUT_FILL_X|LAYOUT_FILL_Y, 0, 70, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    FXHorizontalFrame *rcphf = new FXHorizontalFrame(rcpspring,LAYOUT_FILL_X|LAYOUT_FILL_Y);

    FXSpring *resultspring = new FXSpring(cont, LAYOUT_FILL_X|LAYOUT_FILL_Y, 0, 35);
    FXGroupBox *rgb = new FXGroupBox(resultspring, "Fit output", GROUPBOX_NORMAL|LAYOUT_FILL_X|LAYOUT_FILL_Y|FRAME_GROOVE);
    resulttext = new FXText(rgb, nullptr, 0, TEXT_READONLY|TEXT_WORDWRAP|LAYOUT_FILL_X|LAYOUT_FILL_Y);
    resulttext->setFont(&regressProApp()->monospace_font);

    init_class_list();

    dispers_library_init();

    recipe = new fit_recipe();
    recipe->setup_default_stack();
    m_interactive_fit = new interactive_fit(recipe->stack, recipe->config);

    auto scroll_window = new FXScrollWindow(rcphf, HSCROLLING_OFF | LAYOUT_FILL_Y);
    main_filmstack_window = new filmstack_window(recipe->stack, scroll_window, LAYOUT_FILL_Y | LAYOUT_FILL_X);
    main_filmstack_window->set_target_stack_changes(this, FXSEL(SEL_COMMAND,ID_STACK_CHANGE), FXSEL(SEL_COMMAND,ID_STACK_SHIFT));

    new FXVerticalSeparator(rcphf, SEPARATOR_GROOVE|LAYOUT_FILL_Y);

    main_recipe_window = new recipe_window(recipe, rcphf, LAYOUT_FILL_X|LAYOUT_FILL_Y);
    my_dataset_window = new dataset_window(recipe, this);

    m_filmstack_dialog = new FXDialogBox(this, "Result Film Stack", DECOR_ALL, 0, 0, 400, 300, 0, 0, 0, 0, 0, 0);
    result_filmstack_window = new filmstack_window(m_interactive_fit->stack(), m_filmstack_dialog, LAYOUT_FILL_X|LAYOUT_FILL_Y);
    result_filmstack_window->set_target_stack_changes(this, FXSEL(SEL_COMMAND,ID_STACK_CHANGE), FXSEL(SEL_COMMAND,ID_STACK_SHIFT));
    m_fit_window = new fit_window(m_interactive_fit, this, "Interactive Fit", DECOR_ALL, 0, 0, 640, 480);
    m_fit_window->bind_result_target(&m_interactive_fit_target);
    m_interactive_fit_target.bind(result_filmstack_window);

    m_title_dirty = true;
}

void
regress_pro_window::create()
{
    FXMainWindow::create();
}

long
regress_pro_window::onUpdate(FXObject* sender, FXSelector sel, void* ptr)
{
    FXMainWindow::onUpdate(sender, sel, ptr);

    if(m_title_dirty) {
        FXString filename = recipeFilename.rafter(DIR_SEPARATOR);
        FXString pathname = recipeFilename.rbefore(DIR_SEPARATOR);

        this->setTitle(filename + " - " + pathname + " - Regress Pro");

        m_title_dirty = false;

        return 1;
    }

    if (recipe->ms_setup && !m_enlarged_window) {
        resize(920, 520);
        m_enlarged_window = true;
    } else if (!recipe->ms_setup && m_enlarged_window) {
        resize(720, 520);
        m_enlarged_window = false;
    }

    return 0;
}

void
regress_pro_window::set_spectrum(struct spectrum *new_spectrum)
{
    if(spectrum) {
        spectra_free(spectrum);
    }
    spectrum = new_spectrum;

    m_interactive_fit->bind_spectrum(spectrum);
    m_fit_window->reload();
    main_recipe_window->bind_new_acquisition(spectrum->acquisition);
}

str_ptr regress_pro_window::load_spectrum(const char *filename) {
    str_ptr error_msg;
    struct spectrum *new_spectrum = load_gener_spectrum(filename, &error_msg);
    if(new_spectrum == nullptr) {
        return error_msg;
    }
    set_spectrum(new_spectrum);
    return nullptr;
}

long
regress_pro_window::onCmdLoadSpectra(FXObject*,FXSelector,void *)
{
    regress_pro *app = regressProApp();

    FXFileDialog open(this,"Open Spectra");
    open.setDirectory(app->spectra_dir);
    open.setPatternList(patterns_spectr);

    if(open.execute()) {
        FXString filename = open.getFilename();
        app->spectra_dir = open.getDirectory();

        str_ptr error_msg = load_spectrum(filename.text());
        if(error_msg) {
            FXMessageBox::information(this, MBOX_OK, "Spectra loading", "%s.", CSTR(error_msg));
            free_error_message(error_msg);
        }
        return 1;
    }

    return 0;
}

long
regress_pro_window::onCmdDispersOptim(FXObject*,FXSelector,void*)
{
    if (!m_disp_fit_window) {
        struct disp_fit_engine *fit = disp_fit_engine_new();
        fit->ref_disp = disp_list_search(app_lib, "sio2");
        fit->model_disp = disp_list_search(app_lib, "sio2-ho");
        disp_fit_manager *mgr = new disp_fit_manager(fit);
        m_disp_fit_window = new disp_fit_window(mgr, this, "Dispersion Fit", DECOR_ALL, 0, 0, 640, 480);
        m_disp_fit_window->create();
    }

    m_disp_fit_window->show(FX::PLACEMENT_SCREEN);

    return 1;
}

long
regress_pro_window::onCmdAbout(FXObject *, FXSelector, void *)
{
    FXDialogBox about(this,"About Regress Pro",DECOR_TITLE|DECOR_BORDER,0,0,0,0,
                      0,0,0,0, 0,0);
    FXVerticalFrame* side=new FXVerticalFrame(&about,LAYOUT_SIDE_RIGHT|LAYOUT_FILL_X|LAYOUT_FILL_Y,0,0,0,0, 10,10,10,10, 0,0);
    new FXLabel(side,"R e g r e s s   P r o",nullptr,JUSTIFY_LEFT|ICON_BEFORE_TEXT|LAYOUT_FILL_X);
    new FXHorizontalSeparator(side,SEPARATOR_LINE|LAYOUT_FILL_X);
    FXString release_string = regressProApp()->get_release_string();
    new FXLabel(side,FXString::value("\nRegress Pro, version %s, %s.\n\n" "Regress Pro is a scientific / industrial software to perform regression\nanalysis of measurement data coming from spectroscopic\nellipsometers or reflectometers.\n" "Regress Pro uses the FOX Toolkit version %d.%d.%d.\nCopyright (C) 2005-2015 Francesco Abbate (francesco.bbt@gmail.com).\n",release_string.text(),regressProApp()->get_host_string().text(),FOX_MAJOR,FOX_MINOR,FOX_LEVEL),nullptr,JUSTIFY_LEFT|LAYOUT_FILL_X|LAYOUT_FILL_Y);
    FXButton *button=new FXButton(side,"&OK",nullptr,&about,FXDialogBox::ID_ACCEPT,BUTTON_INITIAL|BUTTON_DEFAULT|FRAME_RAISED|FRAME_THICK|LAYOUT_RIGHT,0,0,0,0,32,32,2,2);
    button->setFocus();
    about.execute(PLACEMENT_OWNER);
    return 1;
}

// Clean up
regress_pro_window::~regress_pro_window()
{
    delete filemenu;
    delete editmenu;
    delete spectrmenu;
    delete dispmenu;
    delete fitmenu;
    delete helpmenu;

    delete recipe;

    if(this->spectrum) {
        spectra_free(this->spectrum);
    }
    delete m_interactive_fit;
}

long
regress_pro_window::onCmdBatchWindow(FXObject *, FXSelector, void *)
{
    if (!my_batch_window) {
        my_batch_window = new batch_window(this);
        my_batch_window->create();
    }
    my_batch_window->show(PLACEMENT_SCREEN);
    return 1;
}

static void free_spectra_list(int n, struct spectrum **spectra_list)
{
    for (int i = 0; i < n; i++) {
        spectra_free(spectra_list[i]);
    }
}

long
regress_pro_window::onCmdRunMultiFit(FXObject*,FXSelector,void *)
{
    if (!recipe->ms_setup) {
        return 0;
    }

    struct multi_fit_engine *fit;
    const fit_parameters *iparams = recipe->ms_setup->iparameters;
    const fit_parameters *cparams = recipe->ms_setup->cparameters;
    dataset_table *dataset = my_dataset_window->dataset();
    int samples_number = dataset->samples_number();
    fit = multi_fit_engine_new(recipe->config, samples_number);
    multi_fit_engine_bind(fit, recipe->stack, recipe->parameters, iparams);
    double *constr_values = new double[cparams->number];
    for (int i = 0; i < samples_number; i++) {
        int error_col;
        bool ok = dataset->get_values(i, cparams, constr_values, error_col);
        if (!ok) {
            FXMessageBox::information(this, MBOX_OK, "Multiple Fit error", "Missing required parameter in dataset.");
            delete [] constr_values;
            multi_fit_engine_free(fit);
            return 1;
        }
        multi_fit_engine_apply_parameters(fit, i, cparams, constr_values);
    }
    delete [] constr_values;

    FXString error_filename;
    bool ok = dataset->get_spectra_list(fit->spectra_list, error_filename);
    if (!ok) {
        FXMessageBox::information(this, MBOX_OK, "Multiple Fit error", "error opening: \"%s\".", error_filename.text());
        multi_fit_engine_free(fit);
        return 1;
    }

    if(multi_fit_engine_prepare(fit) != 0) {
        FXMessageBox::information(this, MBOX_OK, "Multiple Fit error", "Error preparing multi fit calculations.");
        free_spectra_list(samples_number, fit->spectra_list);
        multi_fit_engine_free(fit);
        return 1;
    }

    seeds_list *iseeds = seed_list_new();

    double iseed_values[iparams->number];
    for (int i = 0; i < samples_number; i++) {
        int error_col;
        bool ok = dataset->get_values(i, iparams, iseed_values, error_col);
        if (!ok) {
            FXMessageBox::information(this, MBOX_OK, "Multiple Fit error", "Missing required parameter in dataset.");
            seed_list_free(iseeds);
            multi_fit_engine_disable(fit);
            free_spectra_list(samples_number, fit->spectra_list);
            multi_fit_engine_free(fit);
            return 1;
        }
        for (int j = 0; j < iparams->number; j++) {
            seed_list_add_simple(iseeds, iseed_values[j]);
        }
    }

#ifdef DEBUG
    multi_fit_engine_check_deriv(fit, recipe->seeds, iseeds);
#endif

    str fit_error_msgs;
    ProgressInfo progress(this->getApp(), this);

    str analysis;
    lmfit_multi(fit, recipe->seeds, iseeds,
                &analysis, &fit_error_msgs,
                process_foxgui_events, &progress);

    progress.hide();

    if(fit_error_msgs.len() > 0) {
        FXMessageBox::information(this, MBOX_OK, "Multiple Fit messages", "%s.", fit_error_msgs.text());
    }

    FXString text_fit_result;

    str fp_results;
    multi_fit_engine_print_fit_results(fit, &fp_results);
    text_fit_result.append(fp_results.text());

    text_fit_result.append(analysis.text());

    resulttext->setText(text_fit_result);
    resulttext->setModified(true);

    set_stack_result(stack_copy(fit->stack_list[0]));
    m_result_stack_match = true;

    seed_list_free(iseeds);
    multi_fit_engine_disable(fit);
    free_spectra_list(samples_number, fit->spectra_list);
    multi_fit_engine_free(fit);

    return 1;
}

bool
regress_pro_window::check_spectrum(const char *context)
{
    if(spectrum == nullptr) {
        FXMessageBox::information(this, MBOX_OK, context,
                                  "Please load a spectra before.");
        return false;
    }

    return true;
}

void
regress_pro_window::set_stack_result(stack_t *new_stack)
{
    m_interactive_fit->bind_stack(new_stack);
    m_interactive_fit->generate_spectra();
    m_fit_window->reload();
    result_filmstack_window->bind_new_filmstack(new_stack, false);
}

void
regress_pro_window::update_interactive_fit(fit_engine *fit, const lmfit_result& result)
{
    if (m_result_stack_match) {
        m_interactive_fit->update_from_fit_results(fit);
        m_fit_window->reset_undo();
        m_fit_window->refresh();
        result_filmstack_window->update_values();
    } else {
        set_stack_result(fit_engine_yield_stack(fit));
        m_result_stack_match = true;
    }
    m_fit_window->set_fit_result(result);
}

FXString
regress_pro_window::run_fit(fit_engine *fit, seeds_list *fseeds, struct spectrum *fspectrum)
{
    str analysis;

    gsl::vector x(fit->parameters->number); // Used to store best fit results.
    lmfit_result result;
    if (scriptMode()) {
        nlopt_fit(fit, fspectrum, x, fseeds, &result, &analysis, LMFIT_GET_RESULTING_STACK, nullptr, nullptr);
    } else {
        ProgressInfo progress(this->getApp(), this);
        nlopt_fit(fit, fspectrum, x, fseeds, &result, &analysis, LMFIT_GET_RESULTING_STACK, process_foxgui_events, & progress);
        progress.hide();
    }

    if (result.gsl_status != GSL_SUCCESS) {
        statusbar->getStatusLine()->setNormalText(lmfit_result_error_string(&result));
    } else {
        statusbar->getStatusLine()->setNormalText("Fit Successfull.");
    }

    FXString fitresult, row;

    /* name of the fit recipe */
    row.format("Recipe %s :\n", recipeFilename.text());
    fitresult.append(row);

    /* fit parameters results */
    str fit_parameters_results;
    fit_engine_print_fit_results(fit, x, &fit_parameters_results, 0);
    fitresult.append(fit_parameters_results.text());

    /* final chi square obtained */
    row.format("Residual Chisq/pt: %g\n", result.chisq);
    fitresult.append(row);

    fitresult.append("\n");
    fitresult.append(analysis.text());

    update_interactive_fit(fit, result);

    return fitresult;
}

fit_engine *
prepare_fit_engine(stack_t *stack, fit_parameters *parameters, const fit_config *config, str_ptr *error_msg)
{
    if (parameters->number == 0) {
        *error_msg = new_error_message(FIT_ERROR, "No fitting parameter defined");
        return nullptr;
    }
    if (check_fit_parameters(stack, parameters, error_msg) != 0) {
        return nullptr;
    }
    fit_engine *fit = fit_engine_new();
    fit_engine_bind(fit, stack, config, parameters);
    return fit;
}

str_ptr regress_pro_window::run_fit_command() {
    str_ptr error_msg;
    fit_engine *fit = prepare_fit_engine(recipe->stack, recipe->parameters, recipe->config, &error_msg);
    if (!fit) {
        return error_msg;
    }
    const char *check_error = fit_engine_prepare_check_error(fit, this->spectrum);
    if (check_error) {
        return new_error_message(SPECTRUM_INVALID, check_error);
    }

    FXString fit_result = run_fit(fit, recipe->seeds, this->spectrum);
    fit_engine_free(fit);

    if (scriptMode()) {
        fputs(fit_result.text(), stdout);
    } else {
        resulttext->setText(fit_result);
        resulttext->setModified(true);
    }
    return nullptr;
}

long
regress_pro_window::onCmdRunFit(FXObject*,FXSelector,void *)
{
    if(! check_spectrum("Fitting")) {
        return 0;
    }
    m_fit_window->kill_focus();
    getApp()->runWhileEvents();
    str_ptr error_msg = run_fit_command();
    if (error_msg) {
        FXMessageBox::error(this, MBOX_OK, "Fit running message", "%s.", CSTR(error_msg));
        free_error_message(error_msg);
        return 1;
    }
    getApp()->endWaitCursor();
    return 1;
}

long
regress_pro_window::onCmdInteractiveFit(FXObject*,FXSelector,void*)
{
    m_fit_window->show(FX::PLACEMENT_SCREEN);
    return 1;
}

long
regress_pro_window::onCmdDatasetEdit(FXObject*,FXSelector,void*)
{
    my_dataset_window->show(PLACEMENT_SCREEN);
    return 1;
}

long
regress_pro_window::onCmdStackChange(FXObject *sender,FXSelector,void *)
{
    if ((filmstack_window *) sender == main_filmstack_window) {
        main_recipe_window->handle(this, FXSEL(SEL_COMMAND, recipe_window::ID_STACK_CHANGE), nullptr);
        dataset_table *dataset = my_dataset_window->dataset();
        dataset->handle(this, FXSEL(SEL_COMMAND, dataset_table::ID_STACK_CHANGE), recipe->stack);
        m_result_stack_match = false;
    } else if ((filmstack_window *) sender == result_filmstack_window) {
        m_interactive_fit->update_parameters_list();
        m_interactive_fit->generate_spectra();
        m_fit_window->reload();
        m_result_stack_match = false;
    }
    return 1;
}

long
regress_pro_window::onCmdStackShift(FXObject *sender, FXSelector, void *ptr)
{
    if ((filmstack_window *) sender == main_filmstack_window) {
        recipe->shift_fit_parameters((shift_info *)ptr);
        dataset_table *dataset = my_dataset_window->dataset();
        dataset->handle(this, FXSEL(SEL_COMMAND, dataset_table::ID_STACK_SHIFT), (shift_info *)ptr);
    }
    return 1;
}

float
timeval_subtract(struct timeval *x, struct timeval *y)
{
    float result = y->tv_sec - x->tv_sec;
    result += (y->tv_usec - x->tv_usec) / 1.0E6;
    return result;
}

int
process_foxgui_events(void *data, float progr, const char *msg)
{
    const float wait_time = 0.4, progress_thresold = 0.5;
    ProgressInfo *info = (ProgressInfo *) data;
    struct timeval current[1];

    if(gettimeofday(current, nullptr) != 0) {
        return 0;
    }

    float diff = timeval_subtract(info->start, current);
    const int multfactor = 4096;

    if(info->dialog == nullptr) {
        if(diff < wait_time || progr > progress_thresold) {
            return 0;
        }

        info->dialog = new FXProgressDialog(info->window,
                                            "Fit is running",
                                            "Please wait...",
                                            PROGRESSDIALOG_CANCEL);

        info->dialog->setTotal(multfactor);
        info->dialog->setBarStyle(PROGRESSBAR_PERCENTAGE);
        info->dialog->create();
        info->dialog->show(PLACEMENT_SCREEN);

        info->dialog->setProgress((int)(progr * multfactor));
        info->dialog->repaint();

        info->app->beginWaitCursor();

        return 0;
    }

    if(info->dialog) {
        info->dialog->setProgress((int)(progr * multfactor));
        info->dialog->repaint();
        if(msg) {
            info->dialog->setMessage(msg);
        }

        info->app->runModalWhileEvents(info->dialog);

        return (info->dialog->shown() ? 0 : 1);
    }

    return 0;
}

static int
write_recipe_to_file(fit_recipe *recipe, dataset_table *dataset, const char *filename)
{
    writer_t *w = writer_new();
    recipe->write(w);
    if (dataset) {
        dataset->write(w);
    }
    int status = writer_save_tofile(w, filename);
    writer_free(w);
    return status;
}

void
regress_pro_window::save_recipe_as(const FXString& filename)
{
    dataset_table *dataset = recipe->ms_setup ? my_dataset_window->dataset() : nullptr;
    if (write_recipe_to_file(recipe, dataset, filename.text())) {
        FXMessageBox::error(this, MBOX_OK, "Error Saving Recipe", "Cannot write file \"%s\".", filename.text());
    } else {
        recipeFilename = filename;
        m_title_dirty = true;
   }
}

long
regress_pro_window::onCmdRecipeSaveAs(FXObject *, FXSelector, void *)
{
    regress_pro *app = regressProApp();

    FXFileDialog open(this, "Save Recipe As");
    open.setDirectory(app->recipe_dir);
    open.setPatternList(patterns_recipe);

    while (open.execute()) {
        FXString new_filename = open.getFilename();
        if (new_filename.find('.') < 0) {
            new_filename.append(".rcp");
        }
        app->recipe_dir = open.getDirectory();
        if (FXStat::exists(new_filename)) {
            if (MBOX_CLICKED_YES == FXMessageBox::question(this, MBOX_YES_NO, "Save Recipe Warning", "The file \"%s\" already exists\nAre you sure you want to overwrite it ?", new_filename.text())) {
                save_recipe_as(new_filename);
                break;
            }
        } else {
            save_recipe_as(new_filename);
            break;
        }
    }
    return 1;
}

str_ptr regress_pro_window::load_recipe(const char *filename) {
    str content;
    if(str_loadfile(filename, &content) != 0) {
        return new_error_message(LOADING_FILE_ERROR, "Cannot read file \"%s\".", filename);
    }

    lexer_t *l = lexer_new(content.text());
    fit_recipe *new_recipe = fit_recipe::read(l);
    if (!new_recipe) {
        lexer_free(l);
        return new_error_message(RECIPE_CHECK, "Invalid recipe file \"%s\".", filename);
    }

    dataset_table *dataset = my_dataset_window->dataset();
    if (l->current.tk != TK_EOF) {
        if (dataset->read_update(l)) {
            lexer_free(l);
            return new_error_message(RECIPE_CHECK, "Invalid dataset section in recipe file \"%s\".", filename);
        }
    }
    lexer_free(l);
    recipeFilename = filename;
    m_title_dirty = true;
    fit_recipe *old_recipe = recipe;
    recipe = new_recipe;
    main_recipe_window->bind_new_fit_recipe(recipe);
    main_filmstack_window->bind_new_filmstack(recipe->stack, false);
    set_stack_result(stack_copy(recipe->stack));
    m_result_stack_match = true;
    delete old_recipe;
    return nullptr;
}

long
regress_pro_window::onCmdRecipeLoad(FXObject *, FXSelector, void *)
{
    regress_pro *app = regressProApp();

    FXFileDialog open(this, "Open Fit Recipe");
    open.setDirectory(app->recipe_dir);
    open.setPatternList(patterns_recipe);

    if(open.execute()) {
        FXString filename = open.getFilename();
        app->recipe_dir = open.getDirectory();
        str_ptr error_msg = load_recipe(filename.text());
        if (error_msg) {
            FXMessageBox::error(this, MBOX_OK, "Recipe load", CSTR(error_msg));
            free_error_message(error_msg);
            return 1;
        }
        dataset_table *dataset = my_dataset_window->dataset();
        dataset->handle(this, FXSEL(SEL_COMMAND, dataset_table::ID_STACK_CHANGE), recipe->stack);
    }
    return 1;
}

long regress_pro_window::onCmdResultStack(FXObject *, FXSelector, void *)
{
    m_filmstack_dialog->show(PLACEMENT_OWNER);
    return 1;
}
