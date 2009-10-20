/* Python interface to types.

   Copyright (C) 2008, 2009 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "defs.h"
#include "value.h"
#include "exceptions.h"
#include "python-internal.h"
#include "charset.h"
#include "gdbtypes.h"
#include "cp-support.h"
#include "demangle.h"
#include "objfiles.h"
#include "language.h"

typedef struct pyty_type_object
{
  PyObject_HEAD
  struct type *type;

  /* If a Type object is associated with an objfile, it is kept on a
     doubly-linked list, rooted in the objfile.  This lets us copy the
     underlying struct type when the objfile is deleted.  */
  struct pyty_type_object *prev;
  struct pyty_type_object *next;
} type_object;

static PyTypeObject type_object_type;

/* A Field object.  */
typedef struct pyty_field_object
{
  PyObject_HEAD

  /* Dictionary holding our attributes.  */
  PyObject *dict;
} field_object;

static PyTypeObject field_object_type;

/* This is used to initialize various gdb.TYPE_ constants.  */
struct pyty_code
{
  /* The code.  */
  enum type_code code;
  /* The name.  */
  const char *name;
};

#define ENTRY(X) { X, #X }

static struct pyty_code pyty_codes[] =
{
  ENTRY (TYPE_CODE_PTR),
  ENTRY (TYPE_CODE_ARRAY),
  ENTRY (TYPE_CODE_STRUCT),
  ENTRY (TYPE_CODE_UNION),
  ENTRY (TYPE_CODE_ENUM),
  ENTRY (TYPE_CODE_FLAGS),
  ENTRY (TYPE_CODE_FUNC),
  ENTRY (TYPE_CODE_INT),
  ENTRY (TYPE_CODE_FLT),
  ENTRY (TYPE_CODE_VOID),
  ENTRY (TYPE_CODE_SET),
  ENTRY (TYPE_CODE_RANGE),
  ENTRY (TYPE_CODE_STRING),
  ENTRY (TYPE_CODE_BITSTRING),
  ENTRY (TYPE_CODE_ERROR),
  ENTRY (TYPE_CODE_METHOD),
  ENTRY (TYPE_CODE_METHODPTR),
  ENTRY (TYPE_CODE_MEMBERPTR),
  ENTRY (TYPE_CODE_REF),
  ENTRY (TYPE_CODE_CHAR),
  ENTRY (TYPE_CODE_BOOL),
  ENTRY (TYPE_CODE_COMPLEX),
  ENTRY (TYPE_CODE_TYPEDEF),
  ENTRY (TYPE_CODE_NAMESPACE),
  ENTRY (TYPE_CODE_DECFLOAT),
  ENTRY (TYPE_CODE_INTERNAL_FUNCTION),
  { TYPE_CODE_UNDEF, NULL }
};



static void
field_dealloc (PyObject *obj)
{
  field_object *f = (field_object *) obj;
  Py_XDECREF (f->dict);
  f->ob_type->tp_free (obj);
}

static PyObject *
field_new (void)
{
  field_object *result = PyObject_New (field_object, &field_object_type);
  if (result)
    {
      result->dict = PyDict_New ();
      if (!result->dict)
	{
	  Py_DECREF (result);
	  result = NULL;
	}
    }
  return (PyObject *) result;
}



/* Return the code for this type.  */
static PyObject *
typy_get_code (PyObject *self, void *closure)
{
  struct type *type = ((type_object *) self)->type;
  return PyInt_FromLong (TYPE_CODE (type));
}

/* Helper function for typy_fields which converts a single field to a
   dictionary.  Returns NULL on error.  */
static PyObject *
convert_field (struct type *type, int field)
{
  PyObject *result = field_new ();
  PyObject *arg;

  if (!result)
    return NULL;

  if (!field_is_static (&TYPE_FIELD (type, field)))
    {
      arg = PyLong_FromLong (TYPE_FIELD_BITPOS (type, field));
      if (!arg)
	goto fail;

      if (PyObject_SetAttrString (result, "bitpos", arg) < 0)
	goto failarg;
    }

  if (TYPE_FIELD_NAME (type, field))
    arg = PyString_FromString (TYPE_FIELD_NAME (type, field));
  else
    {
      arg = Py_None;
      Py_INCREF (arg);
    }
  if (!arg)
    goto fail;
  if (PyObject_SetAttrString (result, "name", arg) < 0)
    goto failarg;

  arg = TYPE_FIELD_ARTIFICIAL (type, field) ? Py_True : Py_False;
  Py_INCREF (arg);
  if (PyObject_SetAttrString (result, "artificial", arg) < 0)
    goto failarg;

  arg = PyLong_FromLong (TYPE_FIELD_BITSIZE (type, field));
  if (!arg)
    goto fail;
  if (PyObject_SetAttrString (result, "bitsize", arg) < 0)
    goto failarg;

  /* A field can have a NULL type in some situations.  */
  if (TYPE_FIELD_TYPE (type, field) == NULL)
    {
      arg = Py_None;
      Py_INCREF (arg);
    }
  else
    arg = type_to_type_object (TYPE_FIELD_TYPE (type, field));
  if (!arg)
    goto fail;
  if (PyObject_SetAttrString (result, "type", arg) < 0)
    goto failarg;

  return result;

 failarg:
  Py_DECREF (arg);
 fail:
  Py_DECREF (result);
  return NULL;
}

/* Return a sequence of all fields.  Each field is a dictionary with
   some pre-defined keys.  */
static PyObject *
typy_fields (PyObject *self, PyObject *args)
{
  PyObject *result;
  int i;
  struct type *type = ((type_object *) self)->type;

  /* We would like to make a tuple here, make fields immutable, and
     then memoize the result (and perhaps make Field.type() lazy).
     However, that can lead to cycles.  */
  result = PyList_New (0);

  for (i = 0; i < TYPE_NFIELDS (type); ++i)
    {
      PyObject *dict = convert_field (type, i);
      if (!dict)
	{
	  Py_DECREF (result);
	  return NULL;
	}
      if (PyList_Append (result, dict))
	{
	  Py_DECREF (dict);
	  Py_DECREF (result);
	  return NULL;
	}
    }

  return result;
}

/* Return the type's tag, or None.  */
static PyObject *
typy_get_tag (PyObject *self, void *closure)
{
  struct type *type = ((type_object *) self)->type;
  if (!TYPE_TAG_NAME (type))
    Py_RETURN_NONE;
  return PyString_FromString (TYPE_TAG_NAME (type));
}

/* Return the type, stripped of typedefs. */
static PyObject *
typy_strip_typedefs (PyObject *self, PyObject *args)
{
  struct type *type = ((type_object *) self)->type;

  return type_to_type_object (check_typedef (type));
}

/* Return a Type object which represents a pointer to SELF.  */
static PyObject *
typy_pointer (PyObject *self, PyObject *args)
{
  struct type *type = ((type_object *) self)->type;
  volatile struct gdb_exception except;

  TRY_CATCH (except, RETURN_MASK_ALL)
    {
      type = lookup_pointer_type (type);
    }
  GDB_PY_HANDLE_EXCEPTION (except);

  return type_to_type_object (type);
}

/* Return a Type object which represents a reference to SELF.  */
static PyObject *
typy_reference (PyObject *self, PyObject *args)
{
  struct type *type = ((type_object *) self)->type;
  volatile struct gdb_exception except;

  TRY_CATCH (except, RETURN_MASK_ALL)
    {
      type = lookup_reference_type (type);
    }
  GDB_PY_HANDLE_EXCEPTION (except);

  return type_to_type_object (type);
}

/* Return a Type object which represents the target type of SELF.  */
static PyObject *
typy_target (PyObject *self, PyObject *args)
{
  struct type *type = ((type_object *) self)->type;

  if (!TYPE_TARGET_TYPE (type))
    {
      PyErr_SetString (PyExc_RuntimeError, "type does not have a target");
      return NULL;
    }

  return type_to_type_object (TYPE_TARGET_TYPE (type));
}

/* Return a const-qualified type variant.  */
static PyObject *
typy_const (PyObject *self, PyObject *args)
{
  struct type *type = ((type_object *) self)->type;
  volatile struct gdb_exception except;

  TRY_CATCH (except, RETURN_MASK_ALL)
    {
      type = make_cv_type (1, 0, type, NULL);
    }
  GDB_PY_HANDLE_EXCEPTION (except);

  return type_to_type_object (type);
}

/* Return a volatile-qualified type variant.  */
static PyObject *
typy_volatile (PyObject *self, PyObject *args)
{
  struct type *type = ((type_object *) self)->type;
  volatile struct gdb_exception except;

  TRY_CATCH (except, RETURN_MASK_ALL)
    {
      type = make_cv_type (0, 1, type, NULL);
    }
  GDB_PY_HANDLE_EXCEPTION (except);

  return type_to_type_object (type);
}

/* Return an unqualified type variant.  */
static PyObject *
typy_unqualified (PyObject *self, PyObject *args)
{
  struct type *type = ((type_object *) self)->type;
  volatile struct gdb_exception except;

  TRY_CATCH (except, RETURN_MASK_ALL)
    {
      type = make_cv_type (0, 0, type, NULL);
    }
  GDB_PY_HANDLE_EXCEPTION (except);

  return type_to_type_object (type);
}

/* Return the size of the type represented by SELF, in bytes.  */
static PyObject *
typy_get_sizeof (PyObject *self, void *closure)
{
  struct type *type = ((type_object *) self)->type;
  volatile struct gdb_exception except;

  TRY_CATCH (except, RETURN_MASK_ALL)
    {
      check_typedef (type);
    }
  /* Ignore exceptions.  */

  return PyLong_FromLong (TYPE_LENGTH (type));
}

static struct type *
typy_lookup_typename (char *type_name)
{
  struct type *type = NULL;
  volatile struct gdb_exception except;
  TRY_CATCH (except, RETURN_MASK_ALL)
    {
      if (!strncmp (type_name, "struct ", 7))
	type = lookup_struct (type_name + 7, NULL);
      else if (!strncmp (type_name, "union ", 6))
	type = lookup_union (type_name + 6, NULL);
      else if (!strncmp (type_name, "enum ", 5))
	type = lookup_enum (type_name + 5, NULL);
      else
	type = lookup_typename (python_language, python_gdbarch,
				type_name, NULL, 0);
    }
  if (except.reason < 0)
    {
      PyErr_Format (except.reason == RETURN_QUIT
		    ? PyExc_KeyboardInterrupt : PyExc_RuntimeError,
		    "%s", except.message);
      return NULL;
    }

  return type;
}

static struct type *
typy_lookup_type (struct demangle_component *demangled)
{
  struct type *type;
  char *type_name;
  enum demangle_component_type demangled_type;

  /* Save the type: typy_lookup_type() may (indirectly) overwrite
     memory pointed by demangled.  */
  demangled_type = demangled->type;

  if (demangled_type == DEMANGLE_COMPONENT_POINTER
      || demangled_type == DEMANGLE_COMPONENT_REFERENCE
      || demangled_type == DEMANGLE_COMPONENT_CONST
      || demangled_type == DEMANGLE_COMPONENT_VOLATILE)
    {
      type = typy_lookup_type (demangled->u.s_binary.left);
      if (! type)
	return NULL;

      switch (demangled_type)
	{
	case DEMANGLE_COMPONENT_REFERENCE:
	  return lookup_reference_type (type);
	case DEMANGLE_COMPONENT_POINTER:
	  return lookup_pointer_type (type);
	case DEMANGLE_COMPONENT_CONST:
	  return make_cv_type (1, 0, type, NULL);
	case DEMANGLE_COMPONENT_VOLATILE:
	  return make_cv_type (0, 1, type, NULL);
	}
    }

  type_name = cp_comp_to_string (demangled, 10);
  type = typy_lookup_typename (type_name);
  xfree (type_name);

  return type;
}

static PyObject *
typy_template_argument (PyObject *self, PyObject *args)
{
  int i, argno, n_pointers;
  struct type *type = ((type_object *) self)->type;
  struct demangle_component *demangled;
  const char *err;
  struct type *argtype;

  if (! PyArg_ParseTuple (args, "i", &argno))
    return NULL;

  type = check_typedef (type);
  if (TYPE_CODE (type) == TYPE_CODE_REF)
    type = check_typedef (TYPE_TARGET_TYPE (type));

  if (TYPE_NAME (type) == NULL)
    {
      PyErr_SetString (PyExc_RuntimeError, "null type name");
      return NULL;
    }

  /* Note -- this is not thread-safe.  */
  demangled = cp_demangled_name_to_comp (TYPE_NAME (type), &err);
  if (! demangled)
    {
      PyErr_SetString (PyExc_RuntimeError, err);
      return NULL;
    }

  /* Strip off component names.  */
  while (demangled->type == DEMANGLE_COMPONENT_QUAL_NAME
	 || demangled->type == DEMANGLE_COMPONENT_LOCAL_NAME)
    demangled = demangled->u.s_binary.right;

  if (demangled->type != DEMANGLE_COMPONENT_TEMPLATE)
    {
      PyErr_SetString (PyExc_RuntimeError, "type is not a template");
      return NULL;
    }

  /* Skip from the template to the arguments.  */
  demangled = demangled->u.s_binary.right;

  for (i = 0; demangled && i < argno; ++i)
    demangled = demangled->u.s_binary.right;

  if (! demangled)
    {
      PyErr_Format (PyExc_RuntimeError, "no argument %d in template",
		    argno);
      return NULL;
    }

  argtype = typy_lookup_type (demangled->u.s_binary.left);
  if (! argtype)
    return NULL;

  return type_to_type_object (argtype);
}

static PyObject *
typy_str (PyObject *self)
{
  volatile struct gdb_exception except;
  char *thetype = NULL;
  long length = 0;
  PyObject *result;

  TRY_CATCH (except, RETURN_MASK_ALL)
    {
      struct cleanup *old_chain;
      struct ui_file *stb;

      stb = mem_fileopen ();
      old_chain = make_cleanup_ui_file_delete (stb);

      type_print (type_object_to_type (self), "", stb, -1);

      thetype = ui_file_xstrdup (stb, &length);
      do_cleanups (old_chain);
    }
  if (except.reason < 0)
    {
      xfree (thetype);
      GDB_PY_HANDLE_EXCEPTION (except);
    }

  result = PyUnicode_Decode (thetype, length, host_charset (), NULL);
  xfree (thetype);

  return result;
}



static const struct objfile_data *typy_objfile_data_key;

static void
save_objfile_types (struct objfile *objfile, void *datum)
{
  type_object *obj = datum;
  htab_t copied_types;
  struct cleanup *cleanup;

  /* This prevents another thread from freeing the objects we're
     operating on.  */
  cleanup = ensure_python_env (get_objfile_arch (objfile), current_language);

  copied_types = create_copied_types_hash (objfile);

  while (obj)
    {
      type_object *next = obj->next;

      htab_empty (copied_types);

      obj->type = copy_type_recursive (objfile, obj->type, copied_types);

      obj->next = NULL;
      obj->prev = NULL;

      obj = next;
    }

  htab_delete (copied_types);

  do_cleanups (cleanup);
}

static void
set_type (type_object *obj, struct type *type)
{
  obj->type = type;
  obj->prev = NULL;
  if (type && TYPE_OBJFILE (type))
    {
      struct objfile *objfile = TYPE_OBJFILE (type);

      obj->next = objfile_data (objfile, typy_objfile_data_key);
      if (obj->next)
	obj->next->prev = obj;
      set_objfile_data (objfile, typy_objfile_data_key, obj);
    }
  else
    obj->next = NULL;
}

static void
typy_dealloc (PyObject *obj)
{
  type_object *type = (type_object *) obj;

  if (type->prev)
    type->prev->next = type->next;
  else if (type->type && TYPE_OBJFILE (type->type))
    {
      /* Must reset head of list.  */
      struct objfile *objfile = TYPE_OBJFILE (type->type);
      if (objfile)
	set_objfile_data (objfile, typy_objfile_data_key, type->next);
    }
  if (type->next)
    type->next->prev = type->prev;

  type->ob_type->tp_free (type);
}

/* Create a new Type referring to TYPE.  */
PyObject *
type_to_type_object (struct type *type)
{
  type_object *type_obj;

  type_obj = PyObject_New (type_object, &type_object_type);
  if (type_obj)
    set_type (type_obj, type);

  return (PyObject *) type_obj;
}

struct type *
type_object_to_type (PyObject *obj)
{
  if (! PyObject_TypeCheck (obj, &type_object_type))
    return NULL;
  return ((type_object *) obj)->type;
}



/* Implementation of gdb.lookup_type.  */
PyObject *
gdbpy_lookup_type (PyObject *self, PyObject *args, PyObject *kw)
{
  static char *keywords[] = { "name", NULL };
  char *type_name = NULL;
  struct type *type = NULL;

  if (! PyArg_ParseTupleAndKeywords (args, kw, "s", keywords, &type_name))
    return NULL;

  type = typy_lookup_typename (type_name);
  if (! type)
    return NULL;

  return (PyObject *) type_to_type_object (type);
}

void
gdbpy_initialize_types (void)
{
  int i;

  typy_objfile_data_key
    = register_objfile_data_with_cleanup (save_objfile_types, NULL);

  if (PyType_Ready (&type_object_type) < 0)
    return;
  if (PyType_Ready (&field_object_type) < 0)
    return;

  for (i = 0; pyty_codes[i].name; ++i)
    {
      if (PyModule_AddIntConstant (gdb_module,
				   /* Cast needed for Python 2.4.  */
				   (char *) pyty_codes[i].name,
				   pyty_codes[i].code) < 0)
	return;
    }

  Py_INCREF (&type_object_type);
  PyModule_AddObject (gdb_module, "Type", (PyObject *) &type_object_type);

  Py_INCREF (&field_object_type);
  PyModule_AddObject (gdb_module, "Field", (PyObject *) &field_object_type);
}



static PyGetSetDef type_object_getset[] =
{
  { "code", typy_get_code, NULL,
    "The code for this type.", NULL },
  { "sizeof", typy_get_sizeof, NULL,
    "The size of this type, in bytes.", NULL },
  { "tag", typy_get_tag, NULL,
    "The tag name for this type, or None.", NULL },
  { NULL }
};

static PyMethodDef type_object_methods[] =
{
  { "const", typy_const, METH_NOARGS,
    "const () -> Type\n\
Return a const variant of this type." },
  { "fields", typy_fields, METH_NOARGS,
    "field () -> list\n\
Return a sequence holding all the fields of this type.\n\
Each field is a dictionary." },
  { "pointer", typy_pointer, METH_NOARGS,
    "pointer () -> Type\n\
Return a type of pointer to this type." },
  { "reference", typy_reference, METH_NOARGS,
    "reference () -> Type\n\
Return a type of reference to this type." },
  { "strip_typedefs", typy_strip_typedefs, METH_NOARGS,
    "strip_typedefs () -> Type\n\
Return a type formed by stripping this type of all typedefs."},
  { "target", typy_target, METH_NOARGS,
    "target () -> Type\n\
Return the target type of this type." },
  { "template_argument", typy_template_argument, METH_VARARGS,
    "template_argument (arg) -> Type\n\
Return the type of a template argument." },
  { "unqualified", typy_unqualified, METH_NOARGS,
    "unqualified () -> Type\n\
Return a variant of this type without const or volatile attributes." },
  { "volatile", typy_volatile, METH_NOARGS,
    "volatile () -> Type\n\
Return a volatile variant of this type" },
  { NULL }
};

static PyTypeObject type_object_type =
{
  PyObject_HEAD_INIT (NULL)
  0,				  /*ob_size*/
  "gdb.Type",			  /*tp_name*/
  sizeof (type_object),		  /*tp_basicsize*/
  0,				  /*tp_itemsize*/
  typy_dealloc,			  /*tp_dealloc*/
  0,				  /*tp_print*/
  0,				  /*tp_getattr*/
  0,				  /*tp_setattr*/
  0,				  /*tp_compare*/
  0,				  /*tp_repr*/
  0,				  /*tp_as_number*/
  0,				  /*tp_as_sequence*/
  0,				  /*tp_as_mapping*/
  0,				  /*tp_hash */
  0,				  /*tp_call*/
  typy_str,			  /*tp_str*/
  0,				  /*tp_getattro*/
  0,				  /*tp_setattro*/
  0,				  /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_ITER,  /*tp_flags*/
  "GDB type object",		  /* tp_doc */
  0,				  /* tp_traverse */
  0,				  /* tp_clear */
  0,				  /* tp_richcompare */
  0,				  /* tp_weaklistoffset */
  0,				  /* tp_iter */
  0,				  /* tp_iternext */
  type_object_methods,		  /* tp_methods */
  0,				  /* tp_members */
  type_object_getset,		  /* tp_getset */
  0,				  /* tp_base */
  0,				  /* tp_dict */
  0,				  /* tp_descr_get */
  0,				  /* tp_descr_set */
  0,				  /* tp_dictoffset */
  0,				  /* tp_init */
  0,				  /* tp_alloc */
  0,				  /* tp_new */
};

static PyTypeObject field_object_type =
{
  PyObject_HEAD_INIT (NULL)
  0,				  /*ob_size*/
  "gdb.Field",			  /*tp_name*/
  sizeof (field_object),	  /*tp_basicsize*/
  0,				  /*tp_itemsize*/
  field_dealloc,		  /*tp_dealloc*/
  0,				  /*tp_print*/
  0,				  /*tp_getattr*/
  0,				  /*tp_setattr*/
  0,				  /*tp_compare*/
  0,				  /*tp_repr*/
  0,				  /*tp_as_number*/
  0,				  /*tp_as_sequence*/
  0,				  /*tp_as_mapping*/
  0,				  /*tp_hash */
  0,				  /*tp_call*/
  0,				  /*tp_str*/
  0,				  /*tp_getattro*/
  0,				  /*tp_setattro*/
  0,				  /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_ITER,  /*tp_flags*/
  "GDB field object",		  /* tp_doc */
  0,				  /* tp_traverse */
  0,				  /* tp_clear */
  0,				  /* tp_richcompare */
  0,				  /* tp_weaklistoffset */
  0,				  /* tp_iter */
  0,				  /* tp_iternext */
  0,				  /* tp_methods */
  0,				  /* tp_members */
  0,				  /* tp_getset */
  0,				  /* tp_base */
  0,				  /* tp_dict */
  0,				  /* tp_descr_get */
  0,				  /* tp_descr_set */
  offsetof (field_object, dict),  /* tp_dictoffset */
  0,				  /* tp_init */
  0,				  /* tp_alloc */
  0,				  /* tp_new */
};
