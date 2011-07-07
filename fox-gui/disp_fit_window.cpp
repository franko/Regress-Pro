
#include "disp_fit_window.h"

#include "Strcpp.h"
#include "disp-fit-engine.h"
#include "spectra-path.h"
#include "sampling.h"

static struct fit_parameters * disp_get_all_parameters (const disp_t *d);

// Map
FXDEFMAP(disp_fit_window) disp_fit_window_map[]={
  FXMAPFUNC(SEL_COMMAND, disp_fit_window::ID_PARAM_SELECT, disp_fit_window::onCmdParamSelect),
  FXMAPFUNC(SEL_COMMAND, disp_fit_window::ID_PARAM_VALUE,  disp_fit_window::onCmdParamChange),
  FXMAPFUNC(SEL_CHANGED, disp_fit_window::ID_PARAM_VALUE,  disp_fit_window::onCmdParamChange),
  FXMAPFUNC(SEL_PAINT,   disp_fit_window::ID_CANVAS,       disp_fit_window::onCmdPaint),
  FXMAPFUNC(SEL_UPDATE,  disp_fit_window::ID_CANVAS,       disp_fit_window::onUpdCanvas),
  FXMAPFUNC(SEL_COMMAND, disp_fit_window::ID_RUN_FIT,      disp_fit_window::onCmdRunFit),
  FXMAPFUNC(SEL_COMMAND, disp_fit_window::ID_SPECTR_RANGE, disp_fit_window::onCmdSpectralRange),
  FXMAPFUNC(SEL_CHANGED, disp_fit_window::ID_SPECTR_RANGE, disp_fit_window::onChangeSpectralRange),
};

// Object implementation
FXIMPLEMENT(disp_fit_window,FXMainWindow,disp_fit_window_map,ARRAYNUMBER(disp_fit_window_map));

disp_fit_window::disp_fit_window(elliss_app *app, struct disp_fit_engine *_fit)
  : FXMainWindow(app, "Interactive Fit", NULL, &app->appicon, DECOR_ALL, 0, 0, 640, 480),
    m_fit_engine(_fit), m_canvas_is_dirty(true), m_resize_plot(true), m_always_freeze_plot(true)
{
  // Menubar
  menubar = new FXMenuBar(this, LAYOUT_SIDE_TOP|LAYOUT_FILL_X);
  statusbar = new FXStatusBar(this, LAYOUT_SIDE_BOTTOM|LAYOUT_FILL_X|FRAME_RAISED|STATUSBAR_WITH_DRAGCORNER);

  // fit menu
  fitmenu = new FXMenuPane(this);
  new FXMenuCommand(fitmenu,"&Run",NULL,this,ID_RUN_FIT);
  new FXMenuTitle(menubar,"&Fit",NULL,fitmenu);

  FXHorizontalFrame *mf = new FXHorizontalFrame(this, LAYOUT_FILL_X|LAYOUT_FILL_Y);
  FXScrollWindow *iw = new FXScrollWindow(mf, VSCROLLER_ALWAYS | HSCROLLING_OFF | LAYOUT_FILL_Y);

  FXMatrix *matrix = new FXMatrix(iw, 2, LAYOUT_SIDE_BOTTOM|LAYOUT_FILL_Y|MATRIX_BY_COLUMNS, 0, 0, 0, 0, DEFAULT_SPACING, DEFAULT_SPACING, DEFAULT_SPACING, DEFAULT_SPACING, 1, 1);

  new FXLabel(matrix, "Range");
  m_wl_entry = new FXTextField(matrix, 10, this, ID_SPECTR_RANGE, FRAME_SUNKEN|FRAME_THICK|LAYOUT_FILL_ROW);
  m_wl_entry->setText("240-780,2");
  m_wl_sampling.set(240.0, 780.0, 271);
  update_fit_engine_sampling ();

  m_fit_parameters = fit_parameters_new ();

  struct fit_parameters *params = disp_get_all_parameters (m_fit_engine->model_disp);

  m_parameters.resize(params->number);
  for (unsigned j = 0; j < params->number; j++)
    {
      param_info& p = m_parameters[j];
      p.fp = params->values[j];

      double fpval = disp_get_param_value (m_fit_engine->model_disp, &p.fp);
      p.value = fpval;

      p.selected = false;
    }
  
  fit_parameters_free (params);

  Str pname;
  for (unsigned k = 0; k < m_parameters.size(); k++)
    {
      fit_param_t *fp = &m_parameters[k].fp;

      get_param_name(fp, pname.str());
      FXString fxpname((const FXchar *) pname.cstr());
      FXCheckButton* bt = new FXCheckButton(matrix, fxpname, this, ID_PARAM_SELECT);
      FXTextField* tf = new FXTextField(matrix, 10, this, ID_PARAM_VALUE, FRAME_SUNKEN|FRAME_THICK|TEXTFIELD_REAL|LAYOUT_FILL_ROW);

      param_info* p_inf = m_parameters.data() + k;

      tf->setUserData(p_inf);
      bt->setUserData(p_inf);

      p_inf->text_field = tf;

      FXString fptxt = FXStringFormat("%g", p_inf->value);
      tf->setText(fptxt, true);
    }

  canvas = new FXCanvas(mf, this, ID_CANVAS, LAYOUT_FILL_X|LAYOUT_FILL_Y);

  m_plots.init(app, 2);

  // we take a copy of the model dispersion to avoid the modification
  // of the original object obtained from the script's parsing
  m_fit_engine->model_disp = disp_copy (m_fit_engine->model_disp);

  updatePlot();
}

disp_fit_window::~disp_fit_window() {
  disp_free (m_fit_engine->model_disp);
  disp_fit_engine_free (m_fit_engine);
  fit_parameters_free(m_fit_parameters);
  delete fitmenu;
}

long
disp_fit_window::onCmdParamSelect(FXObject* _cb, FXSelector, void*)
{
  FXCheckButton *cb = (FXCheckButton *) _cb;
  param_info* p_inf = (param_info*) cb->getUserData();
  p_inf->selected = cb->getCheck();
  return 1;
}

void
disp_fit_window::updatePlot()
{
  disp_t *model = m_fit_engine->model_disp;
  disp_t *ref   = m_fit_engine->ref_disp;

  for (unsigned j = 0; j < m_parameters.size(); j++)
    {
      param_info& pi = m_parameters[j];
      dispers_apply_param (model, &pi.fp, pi.value);
    }

  sampling_unif& samp = m_wl_sampling;
  
  plot *p1 = m_plots[0], *p2 = m_plots[1];

  p1->auto_limits(m_resize_plot);
  p2->auto_limits(m_resize_plot);

  disp_plot (ref, model, samp, p1, p2);

  if (m_always_freeze_plot)
    m_resize_plot = false;
}

long
disp_fit_window::onCmdParamChange(FXObject *_txt, FXSelector, void*)
{
  FXTextField *txt = (FXTextField *) _txt;
  FXString vstr = txt->getText();
  param_info* p_inf = (param_info*) txt->getUserData();
  double new_val = strtod (vstr.text(), NULL);

  if (new_val == p_inf->value)
    return 0;

  p_inf->value = new_val;
  m_canvas_is_dirty = true;
  return 1;
}

void
disp_fit_window::update_fit_engine_sampling ()
{
  unsigned nsmp = m_wl_sampling.size();

  gsl_vector *wl;
  if (m_fit_engine->wl && m_fit_engine->wl->size == nsmp)
    {
      wl = m_fit_engine->wl;
    }
  else
    {
      if (m_fit_engine->wl)
	gsl_vector_free (m_fit_engine->wl);

      wl = gsl_vector_alloc (m_wl_sampling.size());
      
      m_fit_engine->wl = wl;
    }

  for (unsigned j = 0; j < nsmp; j++)
    gsl_vector_set (wl, j, m_wl_sampling[j]);
}

bool
disp_fit_window::verify_spectral_range (const char *txt, double ps[])
{
  int nass = sscanf(txt, "%lf-%lf,%lf", ps, ps+1, ps+2);

  if (nass < 3) 
    return false;

  if (ps[2] < 0.1 || ps[0] < 50.0 || ps[1] > 5000.0 || ps[1] < ps[0])
    return false;

  if (int((ps[1]-ps[0])/ps[2]) < 2)
    return false;

  return true;
}

bool
disp_fit_window::update_spectral_range (const char *txt)
{
  double ps[3];

  if (verify_spectral_range (txt, ps))
    {
      m_wl_sampling.set(ps[0], ps[1], int((ps[1]-ps[0])/ps[2]));
      update_fit_engine_sampling ();
      return true;
    }

  return false;
}

long
disp_fit_window::onChangeSpectralRange(FXObject *, FXSelector, void*_txt)
{
  const char * txt = (const char *) _txt;

  if (update_spectral_range (txt))
    {
      m_wl_entry->setTextColor(FXRGB(0,0,0));
      m_canvas_is_dirty = true;
      m_resize_plot = true;
    }
  else
    {
      m_wl_entry->setTextColor(FXRGB(180,0,0));
    }

  return 1;
}

long
disp_fit_window::onCmdSpectralRange(FXObject *, FXSelector, void*)
{
  FXString s = m_wl_entry->getText();
  if (update_spectral_range (s.text()))
    {
      m_canvas_is_dirty = true;
      m_resize_plot = true;
      return 1;
    }
  return 0;
}

void
disp_fit_window::drawPlot()
{
  FXDCWindow dc(canvas);
  int ww = canvas->getWidth(), hh = canvas->getHeight();

  unsigned n = m_plots.size();
  for (unsigned j = 0; j < n; j++)
    m_plots[j]->draw(&dc, ww, hh/n, 0, hh*j/n);

  m_canvas_is_dirty = false;
}

long
disp_fit_window::onCmdPaint(FXObject*, FXSelector, void* ptr)
{
  drawPlot();
  return 1;
}


long
disp_fit_window::onUpdCanvas(FXObject*, FXSelector, void* ptr)
{
  if (m_canvas_is_dirty)
    {
      updatePlot();
      drawPlot();
      return 1;
    }
  return 0;
}

long
disp_fit_window::onCmdRunFit(FXObject*, FXSelector, void* ptr)
{
  if (! m_fit_engine->wl) return 0;

  reg_check_point(this);

  struct fit_parameters* fps = this->m_fit_parameters;

  fit_parameters_clear (fps);

  int fit_params_nb = 0;
  for (unsigned j = 0; j < m_parameters.size(); j++)
    {
      if (m_parameters[j].selected)
	fit_params_nb++;
    }

  gsl_vector* x = gsl_vector_alloc(fit_params_nb);

  unsigned k = 0;
  for (unsigned j = 0; j < m_parameters.size(); j++)
    {
      if (m_parameters[j].selected)
	{
	  fit_parameters_add (fps, &m_parameters[j].fp);
	  gsl_vector_set (x, k, m_parameters[j].value);
	  k ++;
	}
    }

  disp_fit_engine_set_parameters (m_fit_engine, fps);

  struct disp_fit_config cfg[1];
  disp_fit_config_init (cfg);

  double chisq;
  lmfit_disp (m_fit_engine, cfg, x, &chisq, 0, 0);

  k = 0;
  FXString ns;
  for (unsigned j = 0; j < m_parameters.size(); j++)
    {
      if (m_parameters[j].selected)
	{
	  double val = gsl_vector_get (x, k);
	  ns.format("%g", val);
	  m_parameters[j].text_field->setText(ns);
	  m_parameters[j].value = val;
	  k ++;
	}
    }

  gsl_vector_free (x);

  m_canvas_is_dirty = true;

  return 1;
}

struct fit_parameters *
disp_get_all_parameters (const disp_t *d)
{
  struct fit_parameters *fps = fit_parameters_new ();
  int nb = disp_get_number_of_params (d);
  fit_param_t fp = {PID_LAYER_N, 0, d->dclass->model_id, 0};

  for (int j = 0; j < nb; j++)
    {
      fp.param_nb = j;
      fit_parameters_add (fps, &fp);
    }

  return fps;
}
