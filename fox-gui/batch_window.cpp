#include "batch_window.h"
#include "grid-search.h"
#include "regress_pro_window.h"

extern "C" {
    static int window_process_events(void *data, float p, const char *msg);
};

// Map
FXDEFMAP(batch_window) batch_window_map[]= {
    FXMAPFUNC(SEL_COMMAND, batch_window::ID_RUN_BATCH, batch_window::on_cmd_run_batch),
};

FXIMPLEMENT(batch_window,FXDialogBox,batch_window_map,ARRAYNUMBER(batch_window_map));

batch_window::batch_window(regress_pro_window *w, FXuint opts, FXint pl, FXint pr, FXint pt, FXint pb, FXint hs, FXint vs)
    : FXDialogBox(w, "Spectra Dataset", opts, 0, 0, 540, 400, pl, pr, pt, pb, hs, vs),
    app_window(w)
{
    FXHorizontalFrame *hframe = new FXHorizontalFrame(this, LAYOUT_FILL_X|LAYOUT_FILL_Y);
    table = new filelist_table(hframe, NULL, 0, TABLE_COL_SIZABLE|TABLE_ROW_SIZABLE|LAYOUT_FILL_X|LAYOUT_FILL_Y);

    FXVerticalFrame *bframe = new FXVerticalFrame(hframe, LAYOUT_FILL_Y);
    new FXButton(bframe, "Add Files", NULL, table, filelist_table::ID_ADD_FILES);
    new FXButton(bframe, "Remove all", NULL, table, filelist_table::ID_REMOVE_FILES);
    new FXButton(bframe, "Run", NULL, this, ID_RUN_BATCH);
}

int batch_window::batch_run(fit_recipe *recipe, FXString error_msg)
{
    if (recipe->parameters->number == 0 ||
        check_fit_parameters(recipe->stack, recipe->parameters) != 0) {
        error_msg = "Missing fitting parameters";
        return 1;
    }

    fit_engine *fit = fit_engine_new();
    fit_engine_bind(fit, recipe->stack, recipe->config, recipe->parameters);
    // run_fit(fit, recipe->seeds_list, this->spectrum);

    for (int i = 0; i < table->samples_number(); i++) {
        FXString name = table->getItemText(i, 0);
        spectrum *s = load_gener_spectrum(name.text());
        if (!s) {
            error_msg.format("Error loading spectra from file: %s", name.text());
            return 1;
        }
        double chisq;
        fit_engine_prepare(fit, s);
        lmfit_grid(fit, recipe->seeds_list, &chisq, NULL, NULL, LMFIT_PRESERVE_STACK,
                   window_process_events, this);
        fit_engine_disable(fit);
        spectra_free(s);
    }

    fit_engine_free(fit);
    return 0;
}

long batch_window::on_cmd_run_batch(FXObject *, FXSelector, void *)
{
    fit_recipe *recipe = app_window->get_current_recipe();
    if (recipe) {
        FXString error_msg;
        if (batch_run(recipe, error_msg)) {
            FXMessageBox::information(this, MBOX_OK, "Running batch", "Error running the batch: %s\n", error_msg.text());
        }
    }
    return 1;
}

int
window_process_events(void *data, float p, const char *msg)
{
    FXWindow *win = (FXWindow *) data;
    FXApp *app = win->getApp();
    app->runModalWhileEvents(win);
    return 0;
}
