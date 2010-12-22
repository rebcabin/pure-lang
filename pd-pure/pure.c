
/* Copyright (c) 2009 by Albert Graef <Dr.Graef@t-online.de>.  Distributed
   under the new BSD license, see the accompanying COPYING file for details. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <pure/runtime.h>
#include <m_pd.h>

#ifndef VERSION
#define VERSION "0.0"
#endif

#ifndef LIBDIR
#define LIBDIR "/usr/local/lib/pd"
#endif

/* Ticks per millisecond of the internal clock. FIXME: Currently this is a
   fixed value (32.*441000./1000., cf. m_sched.c), will have to be rewritten
   if this changes. */

#define TICKS 14112.0

/* These aren't in the official API, but have been around at least since Pd
   0.39, so are hopefully stable. */

typedef struct _namelist
{
    struct _namelist *nl_next;
    char *nl_string;
} t_namelist;

extern t_namelist *sys_searchpath;

/* Return the hosting Pd version as a string in the format "major.minor". */

extern const char *pd_version_s(void)
{
  static char buf[MAXPDSTRING];
  sprintf(buf, "%d.%d", PD_MAJOR_VERSION, PD_MINOR_VERSION);
  return buf;
}

/* Return the Pd library directory. */

extern const char *pd_libdir_s(void)
{
  return LIBDIR;
}

/* Return the Pd search path, as a list of directory names. */

extern pure_expr *pd_path_sl(void)
{
#if 1
  /* You might have to disable this for some Windows builds of pd.dll which
     for some reason lack sys_searchpath. */
  size_t n;
  pure_expr *xs[1024];
  t_namelist *p = sys_searchpath;
  for (n = 0; p && n < 1024; ) {
    if (p->nl_string) {
      pure_expr *x = pure_cstring_dup(p->nl_string);
      if (x) xs[n++] = x;
    }
    p = p->nl_next;
  }
  return pure_listv(n, xs);
#else
  return pure_listv(0, 0);
#endif
}

/* Alternate interface to Pd's post() routine. The Pd routine can't be used
   directly in Pure since it's a printf-style varargs routine. Our version
   here just provides the capability to post a message string. */

extern void pd_post(const char *s)
{
  post("%s", s);
}

/* Provide access to the current logical Pd time in milliseconds. */

extern double pd_time(void)
{
  return clock_getlogicaltime()/TICKS;
}

/* Provide access to Pd arrays (sample buffers). */

#if PD_MAJOR_VERSION == 0 && PD_MINOR_VERSION < 41
/* Work around broken 64 bit support in older Pd versions. NOTE: This just
   makes the stuff below compile, but won't fix the bugs in Pd itself; you'll
   have to upgrade your Pd version anyway. */
#define garray_getfloatwords(x, size, vec) garray_getfloatarray(x, size, (t_float**)vec)
#endif

/* GSL-compatible double matrix. Pilfered from gsl_structs.h and runtime.cc in
   the core package. */

typedef struct _gsl_block
{
  size_t size;
  double *data;
} gsl_block;

typedef struct _gsl_matrix
{
  size_t size1;
  size_t size2;
  size_t tda;
  double *data;
  gsl_block *block;
  int owner;
} gsl_matrix;

static gsl_matrix*
gsl_matrix_alloc(const size_t n1, const size_t n2)
{
  gsl_block* block;
  gsl_matrix* m;
  if (n1 == 0 || n2 == 0)
    return 0;
  m = (gsl_matrix*)malloc(sizeof(gsl_matrix));
  if (m == 0)
    return 0;
  block = (gsl_block*)malloc(sizeof(gsl_block));
  if (block == 0) {
    free(m);
    return 0;
  }
  block->size = n1*n2;
  block->data = (double*)malloc(block->size*sizeof(double));
  if (block->data == 0) {
    free(m);
    free(block);
    return 0;
  }
  m->data = block->data;
  m->size1 = n1;
  m->size2 = n2;
  m->tda = n2;
  m->block = block;
  m->owner = 1;
  return m;
}

static gsl_matrix*
gsl_matrix_calloc(const size_t n1, const size_t n2)
{
  gsl_matrix* m = gsl_matrix_alloc(n1, n2);
  if (m == 0) return 0;
  memset(m->data, 0, m->block->size*sizeof(double));
  return m;
}

static void gsl_matrix_free(gsl_matrix *m)
{
  if (m->owner) {
    if (m->block) free(m->block->data);
    free(m->block);
  }
  free(m);
}

static inline gsl_matrix*
create_double_matrix(size_t nrows, size_t ncols)
{
  if (nrows == 0 || ncols == 0 ) {
    size_t nrows1 = (nrows>0)?nrows:1;
    size_t ncols1 = (ncols>0)?ncols:1;
    gsl_matrix *m = gsl_matrix_calloc(nrows1, ncols1);
    if (!m) return 0;
    m->size1 = nrows; m->size2 = ncols;
    return m;
  } else
    return gsl_matrix_alloc(nrows, ncols);
}

extern int pd_getbuffersize(const char *name)
{
  t_symbol *sym = gensym((char*)name);
  t_garray *a = (t_garray*)pd_findbyclass(sym, garray_class);
  if (a) {
    int sz;
    t_word *buf;
    if (garray_getfloatwords(a, &sz, &buf))
      return sz;
    else
      return 0;
  } else
    return 0;
}

extern void pd_setbuffersize(const char *name, uint32_t sz)
{
  t_symbol *sym = gensym((char*)name);
  t_garray *a = (t_garray*)pd_findbyclass(sym, garray_class);
  if (a) garray_resize(a, (t_floatarg)sz);
}

extern pure_expr *pd_getbuffer(const char *name)
{
  t_symbol *sym = gensym((char*)name);
  t_garray *a = (t_garray*)pd_findbyclass(sym, garray_class);
  if (a) {
    int sz;
    t_word *buf;
    if (garray_getfloatwords(a, &sz, &buf) && buf) {
      pure_expr *res = matrix_from_double_array(1, sz, 0);
      if (res) {
	gsl_matrix *m = (gsl_matrix*)res->data.mat.p;
	double *p = m->data;
	int i;
	for (i = 0; i < sz; i++)
	  p[i] = (double)buf[i].w_float;
      }
      return res;
    } else
      return 0;
  } else
    return 0;
}

extern void pd_setbuffer(const char *name, pure_expr *x)
{
  uint32_t i, n;
  size_t m;
  int ix;
  pure_expr **xv = 0;
  if (pure_is_tuplev(x, &m, &xv) && m == 2 && pure_is_int(xv[0], &ix))
    x = xv[1];
  else
    ix = 0;
  if (xv) free(xv);
  if (matrix_type(x) == 1 && (n = matrix_size(x)) > 0) {
    gsl_matrix *m = (gsl_matrix*)x->data.mat.p;
    double *p = m->data;
    t_symbol *sym = gensym((char*)name);
    t_garray *a = (t_garray*)pd_findbyclass(sym, garray_class);
    if (a) {
      int sz;
      t_word *buf;
      if (garray_getfloatwords(a, &sz, &buf) && buf) {
	if (ix < 0) ix = 0;
	if (ix > sz) ix = sz;
	if (n > (uint32_t)(sz-ix)) n = (uint32_t)(sz-ix);
	for (i = 0; i < n; i++)
	  buf[ix+i].w_float = (float)p[i];
	garray_redraw(a);
      }
    }
  }
}

/* We maintain a single Pure interpreter instance for all Pure objects. */

static pure_interp *interp = 0;
static int void_sym = 0, delay_sym = 0;

/* The Pure object class. */

typedef struct _pure {
  t_object x_obj;
#ifdef __MINGW32__
  /* This seems to be necessary as some as yet undetermined Pd routine seems
     to write past the end of x_obj on Windows. */
  int fence;			/* dummy field (not used) */
#endif
  struct _pure *next, *prev;	/* double-linked list of all Pure objects */
  /* control inlets and outlets */
  int n_in, n_out;		/* number of extra inlets and outlets */
  struct _px **in;		/* extra inlet proxies, see t_px below */
  t_outlet **out;		/* outlets */
  /* signal inlets and outlets */
  int n_dspin, n_dspout;	/* number of signal inlets and outlets */
  t_sample **dspin, **dspout;	/* signal data */
  gsl_matrix *sig;		/* GSL matrix holding the input signal */
  t_float sr;			/* The samplerate. */
  /* Pure interface */
  pure_expr *foo;		/* the object function */
  char *args;			/* creation arguments */
  char *tmp;			/* temporary storage */
  pure_expr *sigx;		/* Pure expression holding the input signal */
  /* asynchronous messaging */
  t_clock *clock;		/* wakeup for asynchronous processing */
  pure_expr *msg;		/* pending asynchronous message */
} t_pure;

/* Proxy objects for extra inlets (pilfered from flext by Thomas Grill). */

typedef struct _px {
  t_object obj;
#ifdef __MINGW32__
  int fence;
#endif
  int ix;			/* inlet index */
  t_pure *x;			/* parent */
} t_px;

/* The runtime class, which is used to control the Pure runtime environment.
   Responds to 'bang' messages by reloading all scripts and reinitializing all
   Pure objects accordingly. */

typedef struct _runtime {
  t_object obj;
#ifdef __MINGW32__
  int fence;
#endif
  t_outlet *out1, *out2;
} t_runtime;

/* Head and tail of the list of Pure objects. */

static t_pure *xhead = 0, *xtail = 0;

/* We keep track of the different classes in a linked list for now. */

typedef struct _classes {
  t_symbol *sym;
  t_class *class;
  char *dir;
  struct _classes *next;
} t_classes;

static t_classes *pure_classes = 0;
static t_class *px_class = 0, *runtime_class = 0;

static void add_class(t_symbol *sym, t_class *class, char *dir)
{
  t_classes *new = malloc(sizeof(t_classes));
  if (!new) return;
  new->sym = sym;
  new->class = class;
  new->dir = strdup(dir);
  new->next = pure_classes;
  pure_classes = new;
}

static t_class *lookup(t_symbol *sym)
{
  t_classes *act = pure_classes;
  while (act && act->sym != sym) act = act->next;
  if (act)
    return act->class;
  else
    return 0;
}

/* Helper functions to convert between Pd atoms and Pure expressions. */

static bool is_dsp_fun(t_symbol *sym)
{
  int l = strlen(sym->s_name);
  return l>0 && sym->s_name[l-1] == '~';
}

static const char *fun_name_s(const char *name)
{
  int l = strlen(name);
  if (l>0 && name[l-1] == '~') {
    static char buf[1024];
    if (l >= 1020) {
      strncpy(buf, name, 1023);
      buf[1023] = 0;
    } else {
      strncpy(buf, name, l-1);
      strcpy(buf+l-1, "_dsp");
    }
    return buf;
  } else
    return name;
}

static inline const char *fun_name(t_symbol *sym)
{
  return fun_name_s(sym->s_name);
}

static char *get_expr(t_symbol *sym, int argc, t_atom *argv)
{
  t_binbuf *b;
  char *exp_string, *s, *t;
  int exp_strlen, i, l = strcmp(sym->s_name, "pure")?strlen(fun_name(sym))+1:0;

  b = binbuf_new();
  binbuf_add(b, argc, argv);
  binbuf_gettext(b, &exp_string, &exp_strlen);
  exp_string = (char *)t_resizebytes(exp_string, exp_strlen, exp_strlen+1);
  exp_string[exp_strlen] = 0;
  if (!(s = malloc(l+exp_strlen+1)))
    return 0;
  if (l > 0) {
    strcpy(s, fun_name(sym)); s[l-1] = ' ';
  }
  for (t = s+l, i = 0; i < exp_strlen; i++)
    if (exp_string[i] != '\\')
      *(t++) = exp_string[i];
  *t = 0;
  binbuf_free(b);
  freebytes(exp_string, exp_strlen+1);
  if ((t = realloc(s, strlen(s)+1)))
    s = t;
  return s;
}

#define myisalpha(c) (c=='_'||isalpha(c))
#define myisalnum(c) (c=='_'||isalnum(c))

static inline pure_expr *parse_symbol(t_pure *x, const char *s)
{
  size_t i, n = strlen(s);
  if (!myisalpha(s[0])) goto err;
  for (i = 1; i < n; i++)
    if (!myisalnum(s[i])) goto err;
  return pure_symbol(pure_sym(s));
 err:
  /* treat as a Pure string */
  return pure_cstring_dup(s);
}

static inline pure_expr *parse_expr(t_pure *x, const char *s)
{
  pure_expr *y = pure_eval(s);
  if (y == 0)
#if CHECK_SYNTAX
    /* complain about bad Pure syntax */
    pd_error(x, "pd-pure: invalid expression '%s'", s);
#else
    /* cast to a Pure string */
    y = pure_cstring_dup(s);
#endif
  return y;
}

static inline bool is_double(pure_expr *x, double *d)
{
  int i;
  if (pure_is_double(x, d))
    return true;
  else if (pure_is_int(x, &i)) {
    *d = (double)i;
    return true;
  } else
    return false;
}

static inline bool is_delay(pure_expr *x, double *t, pure_expr **msg)
{
  int sym;
  pure_expr *y, *z, *u, *v;
  if (pure_is_app(x, &y, &z) && pure_is_app(y, &u, &v) &&
      pure_is_symbol(u, &sym) && sym == delay_sym) {
    int i;
    if (pure_is_double(v, t))
      ;
    else if (pure_is_int(v, &i))
      *t = (double)i;
    else
      return false;
    *msg = z;
    return true;
  } else
    return false;
}

static inline void create_atom(t_atom *a, pure_expr *x)
{
  char *s;
  double d;
  if (is_double(x, &d))
    SETFLOAT(a, d);
  else {
    t_symbol *sym;
    if (!pure_is_cstring_dup(x, &s)) s = str(x);
    sym = gensym(s?s:"");
    SETSYMBOL(a, sym);
    if (s) free(s);
  }
}

/* Process an output message and route it through the given outlet. */

static inline bool check_outlet(t_pure *x, int k)
{
  /* check outlet index */
#if CHECK_OUTLET
  /* complain about bad outlets */
  if (k < 0) {
    pd_error(x, "pd-pure: bad outlet index %d, must be >= 0", k);
    return 0;
  } else if (x->n_out <= 0) {
    pd_error(x, "pd-pure: bad outlet index %d, object has no outlets", k);
    return 0;
  } else if (k >= x->n_out) {
    pd_error(x, "pd-pure: bad outlet index %d, must be < %d", k, x->n_out);
    return 0;
  } else
    return 1;
#else
  return (k >= 0) && (k < x->n_out);
#endif
}

static void send_message(t_pure *x, int k, pure_expr *y)
{
  char *sval = 0;
  double dval;
  int sym;
  pure_expr *f, **args;
  size_t argc;
  t_atom *argv = 0;
  int i;
  /* Translate Pure lists to corresponding Pd list messages. */
  if (pure_is_listv(y, &argc, &args)) {
    if (argc > 0) {
      argv = malloc(argc*sizeof(t_atom));
      if (!argv) goto errexit;
      for (i = 0; i < argc; i++)
	create_atom(&argv[i], args[i]);
    }
    outlet_list(x->out[k], &s_list, argc, argv);
    goto errexit;
  }
  /* get arguments */
  pure_is_appv(y, &f, &argc, &args);
  /* pd message generation */
  if (pure_is_cstring_dup(f, &sval)) {
    if (!check_outlet(x, k)) goto errexit;
    if (argc == 0)
      /* single symbol value */
      outlet_anything(x->out[k], gensym(sval), 0, NULL);
    else {
      t_symbol *t = gensym(sval);
      if (argc > 0) {
	argv = malloc(argc*sizeof(t_atom));
	if (!argv) goto errexit;
      }
      for (i = 0; i < argc; i++)
	create_atom(&argv[i], args[i]);
      outlet_anything(x->out[k], t, argc, argv);
    }
  } else if (is_double(f, &dval)) {
    if (!check_outlet(x, k)) goto errexit;
    if (argc == 0)
      /* single double value */
      outlet_float(x->out[k], dval);
    else {
      /* create a list message with the double value in front */
      argv = malloc((argc+1)*sizeof(t_atom));
      if (!argv) goto errexit;
      create_atom(&argv[0], f);
      for (i = 0; i < argc; i++)
	create_atom(&argv[i+1], args[i]);
      outlet_list(x->out[k], &s_list, argc+1, argv);
    }
  } else if (pure_is_symbol(f, &sym) && sym > 0 && sym != void_sym) {
    /* manufacture a message with the given symbol as selector */
    const char *pname;
    if (!check_outlet(x, k)) goto errexit;
    if ((pname = pure_sym_pname(sym))) {
       /* FIXME: This should be converted to the system encoding. */
      t_symbol *t = gensym((char*)pname);
      if (argc > 0) {
	argv = malloc(argc*sizeof(t_atom));
	if (!argv) goto errexit;
      }
      for (i = 0; i < argc; i++)
	create_atom(&argv[i], args[i]);
      outlet_anything(x->out[k], t, argc, argv);
    }
  }
 errexit:
  if (sval) free(sval);
  if (args) free(args);
  if (argv) free(argv);
}

/* Schedule a message to be delivered to the object after the given time
   interval (see timeout below). Notes: Negative or zero time values mean that
   the event is scheduled to be delivered immediately. There is only one timer
   per object. If another event has already been scheduled, it is cancelled
   and the new one is scheduled instead. Also, if the time value is inf or
   nan, any existing timer event is cancelled. */

static inline void delay_message(t_pure *x, double t, pure_expr *msg)
{
  const double inf = 1.0e307 * 1.0e307;
  if (x->msg) {
    pure_free(x->msg);
    x->msg = NULL;
  }
  if (t != inf && /* this is false only for nan: */t == t) {
    x->msg = pure_new(msg);
    if (t < 0.0) t = 0.0;
    clock_delay(x->clock, t);
  } else
    clock_unset(x->clock);
}

/* Handle a message to the given inlet. The message is first converted to a
   corresponding Pure expression to which the object function is applied. The
   returned value is processed, converting results back to corresponding Pd
   messages and sending these messages through the appropriate outlets.
   Messages of the form 'delay t' (where t is an int or double time value in
   milliseconds) do not cause any output, but are scheduled to be delivered to
   the object after the given time interval (see timeout below). */

static void receive_message(t_pure *x, t_symbol *s, int k,
			    int argc, t_atom *argv)
{
  size_t i, j, n, m;
  double t;
  pure_expr *f = x->foo, *y, *z, *msg;
  pure_expr **xv = 0, **yv = 0;
  int ix;

  /* check whether we have something to evaluate */
  if (!f) return;

  /* Build the parameter expression from the message. Floats, lists and
     symbols get special treatment, other kinds of objects are passed using
     their string representation. */
  if (argc == 1 && s == &s_float && argv[0].a_type == A_FLOAT)
    y = pure_double(argv[0].a_w.w_float);
  else if (s == &s_list) {
    xv = (argc>0)?malloc(argc*sizeof(pure_expr*)):0;
    for (i = 0; i < argc; i++) {
      if (argv[i].a_type == A_FLOAT)
	z = pure_double(argv[i].a_w.w_float);
      else if (argv[i].a_type == A_SYMBOL)
	z = parse_symbol(x, argv[i].a_w.w_symbol->s_name);
      else {
	char buf[MAXPDSTRING];
	atom_string(argv+i, buf, MAXPDSTRING);
	z = pure_cstring_dup(buf);
      }
      if (z)
	xv[i] = z;
      else {
	for (j = 0; j < i; j++)
	  pure_free(xv[j]);
	free(xv);
	return;
      }
    }
    y = pure_listv(argc, xv);
    if (xv) free(xv); xv = 0;
  } else {
    y = parse_symbol(x, s->s_name);
    for (i = 0; i < argc; i++) {
      if (argv[i].a_type == A_FLOAT)
	z = pure_double(argv[i].a_w.w_float);
      else if (argv[i].a_type == A_SYMBOL)
	z = parse_symbol(x, argv[i].a_w.w_symbol->s_name);
      else {
	char buf[MAXPDSTRING];
	atom_string(argv+i, buf, MAXPDSTRING);
	z = pure_cstring_dup(buf);
      }
      if (z)
	y = pure_app(y, z);
      else {
	pure_freenew(y);
	return;
      }
    }
  }
  /* add the inlet index if needed */
  if (x->n_in > 0)
    y = pure_tuplel(2, pure_int(k), y);
  /* apply the object function */
  y = pure_new(pure_app(f, y));
  /* process the results and route them through the appropriate outlets */
  if (pure_is_listv(y, &n, &xv)) {
    for (i = 0; i < n; i++) {
      if (yv) free(yv); yv = 0;
      if (pure_is_tuplev(xv[i], &m, &yv) && m == 2 && pure_is_int(yv[0], &ix))
	send_message(x, ix, yv[1]);
      else if (is_delay(xv[i], &t, &msg))
	delay_message(x, t, msg);
      else
	send_message(x, 0, xv[i]);
    }
  } else if (pure_is_tuplev(y, &m, &yv) && m == 2 && pure_is_int(yv[0], &ix))
    send_message(x, ix, yv[1]);
  else if (is_delay(y, &t, &msg))
    delay_message(x, t, msg);
  else
    send_message(x, 0, y);
  if (xv) free(xv);
  if (yv) free(yv);
  pure_free(y);
}

/* Timer callback. This works similar to receive_message above, but is called
   when a scheduled clock event arrives. Here, the object function is applied
   to the pending message, and the results are then processed as usual. */

static void timeout(t_pure *x)
{
  size_t i, n, m;
  double t;
  pure_expr *f = x->foo, *y = x->msg, *msg;
  pure_expr **xv = 0, **yv = 0;
  int ix;

  /* check whether we have something to evaluate */
  if (!f || !y) return;
  /* apply the object function */
  y = pure_new(pure_app(f, y));
  pure_free(x->msg); x->msg = 0;
  /* process the results and route them through the appropriate outlets */
  if (pure_is_listv(y, &n, &xv)) {
    for (i = 0; i < n; i++) {
      if (yv) free(yv); yv = 0;
      if (pure_is_tuplev(xv[i], &m, &yv) && m == 2 && pure_is_int(yv[0], &ix))
	send_message(x, ix, yv[1]);
      else if (is_delay(xv[i], &t, &msg))
	delay_message(x, t, msg);
      else
	send_message(x, 0, xv[i]);
    }
  } else if (pure_is_tuplev(y, &m, &yv) && m == 2 && pure_is_int(yv[0], &ix))
    send_message(x, ix, yv[1]);
  else if (is_delay(y, &t, &msg))
    delay_message(x, t, msg);
  else
    send_message(x, 0, y);
  if (xv) free(xv);
  if (yv) free(yv);
  pure_free(y);
}

/* Handle a message to the leftmost inlet. */

static void pure_any(t_pure *x, t_symbol *s, int argc, t_atom *argv)
{
  receive_message(x, s, 0, argc, argv);
}

/* Handle messages to secondary inlets (these are routed through proxies,
   since only the first inlet of a Pd object can actually process arbitrary
   input messages). */

static void px_any(t_px *px, t_symbol *s, int argc, t_atom *argv)
{
  receive_message(px->x, s, px->ix, argc, argv);
}

/* Audio processing methods. */

static t_int *pure_perform(t_int *w)
{
  t_pure *x = (t_pure*)(w[1]);
  pure_expr *y = 0, *z = 0, **xv, **yv;
  int n = (int)(w[2]);
  if (x->foo && x->sigx) {
    gsl_matrix *sig;
    /* get the input data */
    size_t i, j, m = x->n_dspin, tda = x->sig->tda;
    for (i = 0; i < m; i++)
      for (j = 0; j < n; j++)
	x->sig->data[i*tda+j] = (double)x->dspin[i][j];
    /* Invoke the object function. This should return a double matrix with
       numbers of rows and columns corresponding (at least) to the number of
       signal outlets and the block size, respectively. Optionally, it may
       also return other (control) messages to be routed to the control
       outlet. */
    y = pure_new(pure_app(x->foo, x->sigx));
    if (pure_is_tuplev(y, &m, &xv) && m == 2 &&
	pure_is_double_matrix(xv[0], (void**)&sig)) {
      z = xv[1];
      free(xv);
    } else if (!pure_is_double_matrix(y, (void**)&sig)) {
      z = y;
      sig = 0;
    }
    if (sig) {
      /* dsp output */
      m = x->n_dspout; tda = sig->tda;
      if (sig->size1 >= m && sig->size2 >= n) {
	/* get the output data */
	for (i = 0; i < m; i++)
	  for (j = 0; j < n; j++)
	    x->dspout[i][j] = (t_sample)sig->data[i*tda+j];
      }
    }
    if (z) {
      /* process control results and route them through the appropriate
	 outlets */
      int ix;
      double t;
      pure_expr *msg;
      size_t n;
      if (pure_is_listv(z, &n, &xv)) {
	for (i = 0; i < n; i++) {
	  if (pure_is_tuplev(xv[i], &m, &yv) && m == 2 &&
	      pure_is_int(yv[0], &ix)) {
	    send_message(x, ix, yv[1]);
	    free(yv);
	  } else if (is_delay(xv[i], &t, &msg))
	    delay_message(x, t, msg);
	  else
	    send_message(x, 0, xv[i]);
	}
	free(xv);
      } else if (pure_is_tuplev(z, &m, &yv) && m == 2 &&
		 pure_is_int(yv[0], &ix))
	send_message(x, ix, yv[1]);
      else if (is_delay(z, &t, &msg))
	delay_message(x, t, msg);
      else
	send_message(x, 0, z);
    }
  }
 err:
  if (y) pure_free(y);
  return (w+3);
}

static void pure_dsp(t_pure *x, t_signal **sp)
{
  int i, n = sp[0]->s_n;
  t_float sr = sp[0]->s_sr;
  if (x->sr != sr)
    /* The sample rate at which this dsp object is supposed to run. NOTE:
       Currently this value isn't used anywhere, maybe we'd like to pass this
       to the object function in some way? */
    x->sr = sr;
  if (x->n_dspin == 0 && x->n_dspout == 0) return;
  dsp_add(pure_perform, 2, x, n);
  for (i = 0; i < x->n_dspin; i++)
    x->dspin[i] = sp[i+1]->s_vec;
  for (i = 0; i < x->n_dspout; i++)
    x->dspout[i] = sp[x->n_dspin+i+1]->s_vec;
  /* If we haven't created it yet or the block size has changed, prepare a GSL
     matrix and the corresponding Pure expression to be passed to the object
     function. The matrix has one row for each signal inlet and the row size
     (number of columns) is the block size n. (This is true even if the number
     of signal inlets is zero in which case the matrix will be empty.) */
  if (x->sig == NULL || x->sig->size2 != n) {
    if (x->sigx) pure_free(x->sigx);
    x->sig = create_double_matrix(x->n_dspin, n);
    if (x->sig)
      x->sigx = pure_new(pure_double_matrix(x->sig));
    else
      x->sigx = NULL;
  }
}

/* Manage the object list. */

static void xappend(t_pure *x)
{
  x->prev = xtail; x->next = 0;
  if (xtail) xtail->next = x; xtail = x;
  if (!xhead) xhead = x;
}

static void xunlink(t_pure *x)
{
  if (x->prev)
    x->prev->next = x->next;
  else
    xhead = x->next;
  if (x->next)
    x->next->prev = x->prev;
  else
    xtail = x->prev;
}

/* Create a new Pure object. */

static void *pure_init(t_symbol *s, int argc, t_atom *argv)
{
  int i;
  t_pure *x;
  t_class *c = lookup(s);
  bool is_dsp = is_dsp_fun(s);

  if (!c) return 0; /* this shouldn't happen unless we're out of memory */
  x = (t_pure*)pd_new(c);
  xappend(x);

  x->foo = 0;
  x->n_in = 0; x->n_out = 1;
  x->in = 0;
  x->out = 0;
  x->args = get_expr(s, argc, argv);
  x->tmp = 0;
  /* Default setup for a dsp object is 1 control in/out + 1 audio in/out. */
  if (is_dsp)
    x->n_dspin = x->n_dspout = 1;
  else
    x->n_dspin = x->n_dspout = 0;
  x->dspin = x->dspout = 0;
  x->sig = 0;
  x->sigx = 0;
  if (!x->args) {
    pd_error(x, "pd-pure: memory allocation failed");
    return (void *)x;
  }
  x->clock = clock_new(x, (t_method)timeout);
  x->msg = 0;
  /* initialize the object function and determine the number of inlets and
     outlets (these cannot be changed later) */
  if (x->args != 0) {
    int n_in = 1, n_out = 1;
    pure_expr *f = parse_expr(x, x->args);
    x->foo = f;
    if (f) {
      int k = !is_dsp;
      size_t n;
      pure_expr **xv = 0;
      pure_new(f);
      /* handle custom inlet/outlet configurations (n_in,n_out,foo) */
      if (pure_is_tuplev(f, &n, &xv) && n == 3 &&
	  pure_is_int(xv[0], &n_in) && pure_is_int(xv[1], &n_out)) {
	x->foo = pure_new(xv[2]); pure_free(f);
	if (n_in < k) {
	  pd_error(x, "pd-pure: bad number %d of inlets, must be >= %d",
		   n_in, k);
	  n_in = k;
	}
	if (n_out < 0) {
	  pd_error(x, "pd-pure: bad number %d of outlets, must be >= 0",
		   n_out);
	  n_out = 0;
	}
	if (is_dsp) {
	  x->n_dspin = n_in;
	  x->n_dspout = n_out;
	  x->n_in = 0;
	  x->n_out = 1;
	} else {
	  x->n_dspin = 0;
	  x->n_dspout = 0;
	  x->n_in = n_in-1;
	  x->n_out = n_out;
	}
      }
      if (xv) free(xv);
    } else {
      const char *err = lasterr();
      pd_error(x, "pd-pure: error in '%s' creation function", s->s_name);
      if (err && *err) pd_error(x, "pd-pure: %s", err);
    }
  }
  if (x->foo != 0) {
    /* allocate memory for control inlets and outlets */
    if (x->n_in > 0)
      x->in = malloc(x->n_in*sizeof(t_px*));
    if (x->n_out > 0)
      x->out = malloc(x->n_out*sizeof(t_outlet*));
    if (x->n_in > 0 && x->in == 0 ||
	x->n_out > 0 && x->out == 0)
      pd_error(x, "pd-pure: memory allocation failed");
    if (!x->in) x->n_in = 0;
    if (!x->out) x->n_out = 0;
    /* initialize the proxies for the extra control inlets */
    for (i = 0; i < x->n_in; i++) {
      x->in[i] = (t_px*)pd_new(px_class);
      x->in[i]->x = x;
      x->in[i]->ix = i+1;
      inlet_new(&x->x_obj, &x->in[i]->obj.ob_pd, 0, 0);
    }
    /* initialize the control outlets */
    for (i = 0; i < x->n_out; i++)
      x->out[i] = outlet_new(&x->x_obj, 0);
    /* allocate memory for signal inlets and outlets */
    if (x->n_dspin > 0)
      x->dspin = malloc(x->n_dspin*sizeof(t_sample*));
    if (x->n_dspout > 0)
      x->dspout = malloc(x->n_dspout*sizeof(t_sample*));
    if (x->n_dspin > 0 && x->dspin == 0 ||
	x->n_dspout > 0 && x->dspout == 0)
      pd_error(x, "pd-pure: memory allocation failed");
    if (!x->dspin) x->n_dspin = 0;
    if (!x->dspout) x->n_dspout = 0;
    /* initialize signal inlets and outlets */
    for (i = 0; i < x->n_dspin; i++)
      inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    for (i = 0; i < x->n_dspout; i++)
      outlet_new(&x->x_obj, &s_signal);
  } else {
    x->n_in = x->n_out = 0;
    x->n_dspin = x->n_dspout = 0;
  }
  return (void *)x;
}

/* Finalize a Pure object. */

static void pure_fini(t_pure *x)
{
  int i;
  free(x->args);
  if (x->foo) pure_free(x->foo);
  x->foo = 0;
  for (i = 0; i < x->n_in; i++)
    pd_free((t_pd*)x->in[i]);
  if (x->in) free(x->in);
  if (x->out) free(x->out);
  if (x->dspin) free(x->dspin);
  if (x->dspout) free(x->dspout);
  if (x->sigx) pure_free(x->sigx);
  xunlink(x);
}

/* Reinitialize Pure objects in a new interpreter context. */

static void pure_refini(t_pure *x)
{
  pure_free(x->foo);
  pure_free(x->sigx);
  if (x->foo) x->foo = 0;
  if (x->sigx) x->sigx = 0;
  if (x->msg) {
    x->tmp = str(x->msg);
    pure_free(x->msg);
    x->msg = 0;
  }
}

static void pure_reinit(t_pure *x)
{
  if (x->args != 0) {
    int n_in = 1, n_out = 1;
    pure_expr *f = parse_expr(x, x->args);
    x->foo = f;
    if (f) {
      size_t n;
      pure_expr **xv = 0;
      pure_new(f);
      if (pure_is_tuplev(f, &n, &xv) && n == 3 &&
	  pure_is_int(xv[0], &n_in) && pure_is_int(xv[1], &n_out)) {
	x->foo = pure_new(xv[2]); pure_free(f);
	/* FIXME: Number of inlets and outlets are initialized at object
	   creation time, can't change them here. */
      }
      if (xv) free(xv);
    }
  }
  if (x->tmp) {
    x->msg = parse_expr(x, x->tmp);
    free(x->tmp); x->tmp = 0;
  }
}

/* Setup for a Pure object class with the given name. */

static void class_setup(char *name, char *dir)
{
  size_t l = strlen(name);
  bool is_dsp = l>0 && name[l-1]=='~';
  t_symbol *class_s = gensym(name);
  t_class *class =
    class_new(class_s, (t_newmethod)pure_init, (t_method)pure_fini,
	      sizeof(t_pure), CLASS_DEFAULT, A_GIMME, A_NULL);
  if (is_dsp)
    class_addmethod(class, (t_method)pure_dsp, gensym((char*)"dsp"), A_NULL);
  class_addanything(class, pure_any);
  if (is_dsp)
    class_addmethod(class, nullfn, &s_signal, A_NULL);
  class_sethelpsymbol(class, gensym("../../extra/pure/pure-help"));
  add_class(class_s, class, dir);
}

/* Loader setup, pilfered from pd-lua (claudiusmaximus@goto10.org). */

void class_set_extern_dir(t_symbol *s);

static char dirbuf[MAXPDSTRING], cmdbuf[1000];

static int pure_loader(t_canvas *canvas, char *name)
{
  char *ptr;
  int fd = canvas_open(canvas, name, ".pure", dirbuf, &ptr, MAXPDSTRING, 1);
  if (fd >= 0) {
    close(fd);
    class_set_extern_dir(gensym(dirbuf));
    /* Load the Pure script. */
    sprintf(cmdbuf, "using \"%s/%s.pure\";\n", dirbuf, name);
#ifdef VERBOSE
    printf("pd-pure: compiling %s.pure\n", name);
#endif
    pure_evalcmd(cmdbuf);
#ifdef EAGER
    /* Force eager compilation. */
    pure_interp_compile(interp, pure_sym(fun_name_s(name)));
#endif
    /* Create the object class. */
    class_setup(name, dirbuf);
    class_set_extern_dir(&s_);
    return lookup(gensym(name)) != 0;
  } else
    return 0;
}

/* Reload all loaded Pure scripts in a new interpreter instance. */

static void reload(t_classes *c)
{
  /* Walk the list of classes recursively, in postorder, which is the order in
     which the classes were originally created. */
  if (c) {
    reload(c->next);
    if (strcmp(c->sym->s_name, "pure")) {
      class_set_extern_dir(gensym(c->dir));
      sprintf(cmdbuf, "using \"%s/%s.pure\";\n", c->dir, c->sym->s_name);
#ifdef VERBOSE
      printf("pd-pure: compiling %s.pure\n", c->sym->s_name);
#endif
      pure_evalcmd(cmdbuf);
#ifdef EAGER
      pure_interp_compile(interp, pure_sym(fun_name(c->sym)));
#endif
      class_set_extern_dir(&s_);
    }
  }
}

/* Create a new interpreter instance and reinitialize all objects. This must
   be invoked in a safe context, where no evaluations are in progress. */

static void pure_restart(void)
{
  t_pure *x;
  for (x = xhead; x; x = x->next)
    pure_refini(x);
  pure_delete_interp(interp);
  interp = pure_create_interp(0, 0);
  pure_switch_interp(interp);
#ifdef VERBOSE
  printf("pd-pure: reloading, please wait...\n");
#endif
  reload(pure_classes);
  for (x = xhead; x; x = x->next)
    pure_reinit(x);
  void_sym = pure_sym("()");
  delay_sym = pure_sym("pd_delay");
}

/* Methods of the runtime class. */

static void *runtime_init(t_symbol *s, int argc, t_atom *argv)
{
  t_runtime *x = (t_runtime*)pd_new(runtime_class);
  x->out1 = outlet_new(&x->obj, 0);
  x->out2 = outlet_new(&x->obj, 0);
  return (void *)x;
}

static void runtime_any(t_runtime *x, t_symbol *s, int argc, t_atom *argv)
{
  if (s == &s_bang && argc == 0) {
    outlet_bang(x->out2);
    pure_restart();
    outlet_bang(x->out1);
  } else if (s->s_thing)
    if (argc > 0 && argv[0].a_type == A_SYMBOL)
      pd_typedmess(s->s_thing, argv[0].a_w.w_symbol, argc-1, argv+1);
    else
      pd_list(s->s_thing, &s_list, argc, argv);
}

/* Loader setup. */

extern void pure_setup(void)
{
  char buf[MAXPDSTRING];
  char *ptr;
  int fd;
  interp = pure_create_interp(0, 0);
  if (interp) {
    pure_expr *x = pure_symbol(pure_sym("version"));
    char *pure_version = 0;
    pure_is_cstring_dup(x, &pure_version);
    post("pd-pure %s (pure-%s) (c) 2009 Albert Graef <Dr.Graef@t-online.de>", VERSION, pure_version);
    post("pd-pure: compiled for pd-%d.%d on %s %s", PD_MAJOR_VERSION, PD_MINOR_VERSION, __DATE__, __TIME__);
    if (pure_version) free (pure_version);
    /* Register the loader for Pure externals. */
    sys_register_loader(pure_loader);
    /* Create the proxy class for extra inlets. */
    px_class = class_new(gensym("pure proxy"), 0, 0,
			 sizeof(t_px), CLASS_PD|CLASS_NOINLET,
			 A_NULL);
    class_addanything(px_class, px_any);
    /* Create the runtime class. */
    runtime_class = class_new(gensym("pure-runtime"),
			      (t_newmethod)runtime_init, 0,
			      sizeof(t_runtime), CLASS_DEFAULT,
			      A_GIMME, A_NULL);
    class_addanything(runtime_class, runtime_any);
    class_sethelpsymbol(runtime_class, gensym("pure-help"));
    /* Create a class for 'pure' objects which allows you to access any
       predefined Pure function without loading a script. */
    class_setup("pure", "");
    /* Look up a few symbols that we need. */
    void_sym = pure_sym("()");
    delay_sym = pure_sym("pd_delay");
  } else
    error("pd-pure: error initializing interpreter; loader not registered");
}
