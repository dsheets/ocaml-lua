#include <string.h>
#include <stdarg.h>

#include <lua5.1/lua.h>
#include <lua5.1/lauxlib.h>
#include <lua5.1/lualib.h>

#include <caml/mlvalues.h>
#include <caml/memory.h>
#include <caml/alloc.h>
#include <caml/custom.h>
#include <caml/fail.h>
#include <caml/callback.h>
#include <caml/signals.h>

/******************************************************************************/
/*****                           DEBUG FUNCTION                           *****/
/******************************************************************************/
/* Comment out the following line to enable debug */
#define NO_DEBUG

#if defined(NO_DEBUG) && defined(__GNUC__)
#define debug(level, format, args...) ((void)0)
#else
void debug(int level, char *format, ...);
#endif

#ifndef NO_DEBUG
static int msglevel = 4; /* the higher, the more messages... */
#endif

#if defined(NO_DEBUG) && defined(__GNUC__)
/* Nothing */
#else
void debug(int level, char* format, ...)
{
#ifdef NO_DEBUG
    /* Empty body, so a good compiler will optimise calls
       to debug away */
#else
    va_list args;

    if (level > msglevel)
        return;

    va_start(args, format);
    vfprintf(stderr, format, args);
    fflush(stderr);
    va_end(args);
#endif /* NO_DEBUG */
}
#endif /* NO_DEBUG && __GNUC__ */


/******************************************************************************/
/*****                           UTILITY MACROS                           *****/
/******************************************************************************/
/* Library unique ID */
#define UUID "551087dd-4133-4097-87c6-79c27cde5c15"

/* Access the lua_State inside an OCaml custom block */
#define lua_State_val(L) (*((lua_State **) Data_custom_val(L))) /* also l-value */


/******************************************************************************/
/*****                    MACROS FOR BOILERPLATE CODE                     *****/
/******************************************************************************/
/* For Lua function with signature : lua_State -> void */
#define STUB_STATE_VOID(lua_function) \
CAMLprim \
value lua_function##__stub(value L) \
{ \
    CAMLparam1(L); \
    lua_function(lua_State_val(L)); \
    CAMLreturn(Val_unit); \
}

/* For Lua function with signature : lua_State -> int */
#define STUB_STATE_INT(lua_function) \
CAMLprim \
value lua_function##__stub(value L) \
{ \
    CAMLparam1(L); \
    int retval = lua_function(lua_State_val(L)); \
    CAMLreturn(Val_int(retval)); \
}

/* For Lua function with signature : lua_State -> int -> int -> int */
#define STUB_STATE_INT_INT_INT(lua_function, int1_name, int2_name) \
CAMLprim \
value lua_function##__stub(value L, value int1_name, value int2_name) \
{ \
    CAMLparam3(L, int1_name, int2_name); \
    int retval = lua_function(lua_State_val(L), Int_val(int1_name), Int_val(int2_name)); \
    CAMLreturn(Val_int(retval)); \
}

/* For Lua function with signature : lua_State -> int -> int */
#define STUB_STATE_INT_INT(lua_function, int_name) \
CAMLprim \
value lua_function##__stub(value L, value int_name) \
{ \
    CAMLparam2(L, int_name); \
    int retval = lua_function(lua_State_val(L), Int_val(int_name)); \
    CAMLreturn(Val_int(retval)); \
}

/* For Lua function with signature : lua_State -> int -> void */
#define STUB_STATE_INT_VOID(lua_function, int_name) \
CAMLprim \
value lua_function##__stub(value L, value int_name) \
{ \
    CAMLparam2(L, int_name); \
    lua_function(lua_State_val(L), Int_val(int_name)); \
    CAMLreturn(Val_unit); \
}

/* For Lua function with signature : lua_State -> double -> void */
#define STUB_STATE_DOUBLE_VOID(lua_function, double_name) \
CAMLprim \
value lua_function##__stub(value L, value double_name) \
{ \
    CAMLparam2(L, double_name); \
    lua_function(lua_State_val(L), Double_val(double_name)); \
    CAMLreturn(Val_unit); \
}

/* For Lua function with signature : lua_State -> bool -> void */
#define STUB_STATE_BOOL_VOID(lua_function, bool_name) \
CAMLprim \
value lua_function##__stub(value L, value bool_name) \
{ \
    CAMLparam2(L, bool_name); \
    lua_function(lua_State_val(L), Bool_val(bool_name)); \
    CAMLreturn(Val_unit); \
}

/* For Lua function with signature : lua_State -> int -> int -> void */
#define STUB_STATE_INT_INT_VOID(lua_function, int1_name, int2_name) \
CAMLprim \
value lua_function##__stub(value L, value int1_name, value int2_name) \
{ \
  CAMLparam3(L, int1_name, int2_name); \
  lua_function(lua_State_val(L), Int_val(int1_name), Int_val(int2_name)); \
  CAMLreturn(Val_unit); \
}

/* For Lua function with signature : lua_State -> int -> bool */
#define STUB_STATE_INT_BOOL(lua_function, int_name) \
CAMLprim \
value lua_function##__stub(value L, value int_name) \
{ \
  CAMLparam2(L, int_name); \
  int retval = lua_function(lua_State_val(L), Int_val(int_name)); \
  if (retval == 0) \
    CAMLreturn(Val_false); \
  else \
    CAMLreturn(Val_true); \
}

/* For Lua function with signature : lua_State -> int -> int -> bool */
#define STUB_STATE_INT_INT_BOOL(lua_function, int1_name, int2_name) \
CAMLprim \
value lua_function##__stub(value L, value int1_name, value int2_name) \
{ \
  CAMLparam3(L, int1_name, int2_name); \
  int retval = lua_function(lua_State_val(L), Int_val(int1_name), Int_val(int2_name)); \
  if (retval == 0) \
    CAMLreturn(Val_false); \
  else \
    CAMLreturn(Val_true); \
}


/******************************************************************************/
/*****                          DATA STRUCTURES                           *****/
/******************************************************************************/
typedef struct ocaml_data
{
    value panic_callback;
    int ref_counter;
} ocaml_data;

static void finalize_lua_State(value L); /* Forward declaration */

static struct custom_operations lua_State_ops =
{
  UUID,
  finalize_lua_State,
  custom_compare_default,
  custom_hash_default,
  custom_serialize_default,
  custom_deserialize_default
};


/******************************************************************************/
/*****                         UTILITY FUNCTIONS                          *****/
/******************************************************************************/
static void *custom_alloc ( void *ud,
                            void *ptr,
                            size_t osize,
                            size_t nsize )
{
    (void)ud;
    (void)osize;  /* not used */
    void *realloc_result = NULL;

    debug(3, "custom_alloc(%p, %p, %d, %d)\n", ud, ptr, osize, nsize);

    if (nsize == 0)
    {
        debug(4, "    custom_alloc: calling free(%p)\n", ptr);
        free(ptr);
        debug(4, "    custom_alloc: returning NULL\n");
        return NULL;
    }
    else
    {
        debug(4, "    custom_alloc: calling caml_stat_resize(%p, %d)\n", ptr, nsize);
        realloc_result = caml_stat_resize(ptr, nsize);
        debug(4, "    custom_alloc: returning %p\n", realloc_result);
        return realloc_result;
    }
}


static int closure_data_gc(lua_State *L)
{
    value *ocaml_closure = (value*)lua_touserdata(L, 1);
    caml_remove_global_root(ocaml_closure);
    return 0;
}


static void set_ocaml_data(lua_State *L, ocaml_data* data)
{
    lua_newtable(L);                          /* Table (t) for our private date */
    lua_pushstring(L, "ocaml_data");
    lua_pushlightuserdata(L, (void *)data);
    lua_settable(L, -3);                      /* t["ocaml_data"] = our_private_data */

    lua_newtable(L);                          /* metatable for userdata used by lua_pushcfunction__stub */
    lua_pushstring(L, "__gc");
    lua_pushcfunction(L, closure_data_gc);
    lua_settable(L, -3);

    lua_pushstring(L, "closure_metatable");
    lua_insert(L, -2);
    lua_settable(L, -3);                      /* t["closure_metatable"] = metatable_for_closures */

    lua_pushstring(L, UUID);
    lua_insert(L, -2);
    lua_settable(L, LUA_REGISTRYINDEX);       /* registry[UUID] = t */
}


static ocaml_data * get_ocaml_data(lua_State *L)
{
    lua_pushstring(L, UUID);
    lua_gettable(L, LUA_REGISTRYINDEX);
    lua_pushstring(L, "ocaml_data");
    lua_gettable(L, -2);
    ocaml_data *info = (ocaml_data*)lua_touserdata(L, -1);
    lua_pop(L, 2);
    return info;
}


static int panic_wrapper(lua_State *L)
{
    CAMLlocal1(v_L);
    ocaml_data *data = get_ocaml_data(L);

    /* wrap the lua_State* in a custom object */
    v_L = caml_alloc_custom(&lua_State_ops, sizeof(lua_State *), 1, 10);
    lua_State_val(v_L) = L;
    data->ref_counter++;

    return Int_val(caml_callback(data->panic_callback, v_L));
}


static void finalize_lua_State(value L)
{
    lua_State *state = lua_State_val(L);
    ocaml_data *data = get_ocaml_data(state);

    if (data->ref_counter == 1)
    {
        caml_remove_global_root(&(data->panic_callback));
        caml_stat_free(data);
        lua_close(state);
    }
    else
    {
        data->ref_counter--;
    }
}


static int execute_ocaml_closure(lua_State *L)
{
    CAMLlocal1(v_L);

    value *ocaml_closure = (value*)lua_touserdata(L, lua_upvalueindex(1));
    ocaml_data *data = get_ocaml_data(L);

    /* wrap the lua_State* in a custom object */
    v_L = caml_alloc_custom(&lua_State_ops, sizeof(lua_State *), 1, 10);
    lua_State_val(v_L) = L;
    data->ref_counter++;

    return Int_val(caml_callback(*ocaml_closure, v_L));
}

/******************************************************************************/
/*****                           LUA API STUBS                            *****/
/******************************************************************************/
CAMLprim
value lua_atpanic__stub(value L, value panicf)
{
    CAMLparam2(L, panicf);
    CAMLlocal1(old_panicf);

    lua_State *state = lua_State_val(L);

    ocaml_data *data = get_ocaml_data(state);

    old_panicf = data->panic_callback;
    caml_remove_global_root(&(data->panic_callback));
    caml_register_global_root(&(data->panic_callback));
    data->panic_callback = panicf;
    lua_atpanic(state, panic_wrapper);

    CAMLreturn(old_panicf);
}

STUB_STATE_INT_INT_VOID(lua_call, nargs, nresults)

STUB_STATE_INT_BOOL(lua_checkstack, extra)

STUB_STATE_INT_VOID(lua_concat, n)

STUB_STATE_INT_INT_VOID(lua_createtable, narr, nrec)

STUB_STATE_INT_INT_BOOL(lua_equal, index1, index2)

STUB_STATE_VOID(lua_error)

STUB_STATE_INT_INT_INT(lua_gc, what, data)

STUB_STATE_INT_VOID(lua_getfenv, index)

CAMLprim
value lua_getfield__stub(value L, value index, value k)
{
    CAMLparam3(L, index, k);
    lua_getfield(lua_State_val(L), Int_val(index), String_val(k));
    CAMLreturn(Val_unit);
}

STUB_STATE_INT_INT(lua_getmetatable, index)

STUB_STATE_INT_VOID(lua_gettable, index)

STUB_STATE_INT(lua_gettop)

STUB_STATE_INT_VOID(lua_insert, index)

STUB_STATE_INT_BOOL(lua_isboolean, index)

STUB_STATE_INT_BOOL(lua_iscfunction, index)

STUB_STATE_INT_BOOL(lua_isfunction, index)

STUB_STATE_INT_BOOL(lua_islightuserdata, index)

STUB_STATE_INT_BOOL(lua_isnil, index)

STUB_STATE_INT_BOOL(lua_isnone, index)

STUB_STATE_INT_BOOL(lua_isnoneornil, index)

STUB_STATE_INT_BOOL(lua_isnumber, index)

STUB_STATE_INT_BOOL(lua_isstring, index)

STUB_STATE_INT_BOOL(lua_istable, index)

STUB_STATE_INT_BOOL(lua_isthread, index)

STUB_STATE_INT_BOOL(lua_isuserdata, index)

STUB_STATE_INT_INT_BOOL(lua_lessthan, index1, index2)

STUB_STATE_VOID(lua_newtable)

STUB_STATE_INT_INT(lua_next, index)

STUB_STATE_INT_INT(lua_objlen, index)

CAMLprim
value lua_pcall__stub(value L, value nargs, value nresults, value errfunc)
{
  CAMLparam4(L, nargs, nresults, errfunc);
  CAMLlocal1(status);

  status = Val_int(lua_pcall( lua_State_val(L),
                              Int_val(nargs),
                              Int_val(nresults),
                              Int_val(errfunc)) );
  CAMLreturn(status);
}

STUB_STATE_INT_VOID(lua_pop, n)

STUB_STATE_BOOL_VOID(lua_pushboolean, b)

CAMLprim
value lua_pushcfunction__stub(value L, value f)
{
    CAMLparam2(L, f);

    /* Create the new userdatum containing the OCaml value of the closure */
    lua_State *LL = lua_State_val(L);
    value *ocaml_closure = (value*)lua_newuserdata(LL, sizeof(value));
    
    caml_register_global_root(ocaml_closure);
    *ocaml_closure = f;

    /* retrieve the metatable for this kind of userdata */
    lua_pushstring(LL, UUID);
    lua_gettable(LL, LUA_REGISTRYINDEX);
    lua_pushstring(LL, "closure_metatable");
    lua_gettable(LL, -2);
    lua_setmetatable(LL, -3);
    lua_pop(LL, 1);

    /* at this point the stack has a userdatum on its top, with the correct metatable */

    lua_pushcclosure(LL, execute_ocaml_closure, 1);

    CAMLreturn(Val_unit);
}

STUB_STATE_INT_VOID(lua_pushinteger, n)

CAMLprim
value lua_pushlstring__stub(value L, value s)
{
    CAMLparam2(L, s);
    lua_pushlstring(lua_State_val(L), String_val(s), caml_string_length(s));
    CAMLreturn(Val_unit);
}

STUB_STATE_VOID(lua_pushnil)

STUB_STATE_DOUBLE_VOID(lua_pushnumber, n)

STUB_STATE_INT_VOID(lua_pushvalue, index)

STUB_STATE_INT_INT_BOOL(lua_rawequal, index1, index2)

STUB_STATE_INT_VOID(lua_rawget, index)

STUB_STATE_INT_INT_VOID(lua_rawgeti, index, n)

STUB_STATE_INT_VOID(lua_rawset, index)

STUB_STATE_INT_INT_VOID(lua_rawseti, index, n)

STUB_STATE_INT_VOID(lua_remove, index)

STUB_STATE_INT_VOID(lua_replace, index)

STUB_STATE_INT_BOOL(lua_setfenv, index)

CAMLprim
value lua_setfield__stub(value L, value index, value k)
{
    CAMLparam3(L, index, k);
    lua_setfield(lua_State_val(L), Int_val(index), String_val(k));
    CAMLreturn(Val_unit);
}

CAMLprim
value lua_setglobal__stub(value L, value name)
{
    CAMLparam2(L, name);
    lua_setglobal(lua_State_val(L), String_val(name));
    CAMLreturn(Val_unit);
}

STUB_STATE_INT_INT(lua_setmetatable, index)

STUB_STATE_INT_VOID(lua_settable, index)

STUB_STATE_INT_VOID(lua_settop, index)

STUB_STATE_INT(lua_status)

STUB_STATE_INT_BOOL(lua_toboolean, index)

CAMLprim
value lua_tocfunction__stub(value L, value index)
{
    CAMLparam2(L, index);

    lua_State *LL = lua_State_val(L);

    /* 1ST: get the C function pointer */
    lua_CFunction f = lua_tocfunction(LL, Int_val(index));

    /* If the Lua object at position "index" of the stack is not a C function,
       raise an exception (will be catched on the OCaml side */
    if (f == NULL) caml_raise_constant(*caml_named_value("Not_a_C_function"));

    /* 2ND: if the function is an OCaml closure, pushed in the Lua world using
       Lua.pushcfunction, the pointer returned here is actually
       execute_ocaml_closure. This is a static function defined in this file and
       without a context it's useless. Instead of returning a "value" version of
       execute_ocaml_closure we get the upvalue, which is the original value of
       the OCaml closure, and return it. */

    /* lua_getupvalue (from the Lua debugging interface) pushes the upvalue on
       the stack (+1) */
    const char *name = lua_getupvalue(LL, Int_val(index), 1);
    if (name == NULL)
    {
        /* In case of error, raise an exception */
        caml_raise_constant(*caml_named_value("Not_a_C_function"));
    }
    else
    {
        /* Convert the userdatum to an OCaml value */
        value *ocaml_closure = (value*)lua_touserdata(LL, -1);

        /* remove the userdatum from the stack (-1) */
        lua_pop(LL, 1);

        /* return it */
        CAMLreturn (*ocaml_closure);
    }
}


/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
static int default_panic(lua_State *L)
{
    CAMLlocal1(v_L);

    value *default_panic_v = caml_named_value("default_panic");
    ocaml_data *data = get_ocaml_data(L);

    /* wrap the lua_State* in a custom object */
    v_L = caml_alloc_custom(&lua_State_ops, sizeof(lua_State *), 1, 10);
    lua_State_val(v_L) = L;
    data->ref_counter++;

    return Int_val(caml_callback(*default_panic_v, v_L));
}


CAMLprim
value luaL_newstate__stub (value unit)
{
    CAMLparam1(unit);
    CAMLlocal1(v_L);

    value *default_panic_v = caml_named_value("default_panic");

    /* create a fresh new Lua state */
    lua_State *L = lua_newstate(custom_alloc, NULL);
    debug(3, "luaL_newstate__stub: calling lua_newstate -> %p\n", (void*)L);
    lua_atpanic(L, &default_panic);

    /* alloc space for the register entry */
    ocaml_data *data = (ocaml_data*)caml_stat_alloc(sizeof(ocaml_data));
    caml_register_global_root(&(data->panic_callback));
    data->panic_callback = *default_panic_v;
    data->ref_counter = 1;

    /* create a new Lua table for binding informations */
    set_ocaml_data(L, data);

    /* wrap the lua_State* in a custom object */
    v_L = caml_alloc_custom(&lua_State_ops, sizeof(lua_State *), 1, 10);
    lua_State_val(v_L) = L;

    /* return the lua_State value */
    CAMLreturn(v_L);
}

CAMLprim
value luaL_loadbuffer__stub(value L, value buff, value sz, value name)
{
  CAMLparam4(L, buff, sz, name);
  CAMLlocal1(status);

  status = Val_int(luaL_loadbuffer( lua_State_val(L),
                                    String_val(buff),
                                    Int_val(sz),
                                    String_val(name)) );
  CAMLreturn(status);
}




void raise_type_error(char *msg)
{
  caml_raise_with_string(*caml_named_value("Lua_type_error"), msg);
}


CAMLprim
value lua_tolstring__stub(value L, value index)
{
  size_t len = 0;
  const char *value_from_lua;
  CAMLparam2(L, index);
  CAMLlocal1(ret_val);

  value_from_lua = lua_tolstring( lua_State_val(L),
                                  Int_val(index),
                                  &len );
  if (value_from_lua != NULL)
  {
    ret_val = caml_alloc_string(len);
    char *s = String_val(ret_val);
    memcpy(s, value_from_lua, len);
  }
  else
  {
    raise_type_error("lua_tolstring: not a string value!");
  }

  CAMLreturn(ret_val);
}

CAMLprim
value luaL_openlibs__stub(value L)
{
  CAMLparam1(L);
  luaL_openlibs(lua_State_val(L));
  CAMLreturn(Val_unit);
}

