/*
 * Copyright (C) 2018 Facebook
 *
 * This file is part of libbtrfsutil.
 *
 * libbtrfsutil is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libbtrfsutil is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libbtrfsutil.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "btrfsutilpy.h"

static int fd_converter(PyObject *o, void *p)
{
	int *fd = p;
	long tmp;
	int overflow;

	tmp = PyLong_AsLongAndOverflow(o, &overflow);
	if (tmp == -1 && PyErr_Occurred())
		return 0;
	if (overflow > 0 || tmp > INT_MAX) {
		PyErr_SetString(PyExc_OverflowError,
				"fd is greater than maximum");
		return 0;
	}
	if (overflow < 0 || tmp < 0) {
		PyErr_SetString(PyExc_ValueError, "fd is negative");
		return 0;
	}
	*fd = tmp;
	return 1;
}

int path_converter(PyObject *o, void *p)
{
	struct path_arg *path = p;
	int is_index, is_bytes, is_unicode;
	PyObject *bytes = NULL;
	Py_ssize_t length = 0;
	char *tmp;

	if (o == NULL) {
		path_cleanup(p);
		return 1;
	}

	path->object = path->cleanup = NULL;
	Py_INCREF(o);

	path->fd = -1;

	is_index = path->allow_fd && PyIndex_Check(o);
	is_bytes = PyBytes_Check(o);
	is_unicode = PyUnicode_Check(o);

	if (!is_index && !is_bytes && !is_unicode) {
		_Py_IDENTIFIER(__fspath__);
		PyObject *func;

		func = _PyObject_LookupSpecial(o, &PyId___fspath__);
		if (func == NULL)
			goto err_format;
		Py_DECREF(o);
		o = PyObject_CallFunctionObjArgs(func, NULL);
		Py_DECREF(func);
		if (o == NULL)
			return 0;
		is_bytes = PyBytes_Check(o);
		is_unicode = PyUnicode_Check(o);
	}

	if (is_unicode) {
		if (!PyUnicode_FSConverter(o, &bytes))
			goto err;
	} else if (is_bytes) {
		bytes = o;
		Py_INCREF(bytes);
	} else if (is_index) {
		if (!fd_converter(o, &path->fd))
			goto err;
		path->path = NULL;
		goto out;
	} else {
err_format:
		PyErr_Format(PyExc_TypeError, "expected %s, not %s",
			     path->allow_fd ? "string, bytes, os.PathLike, or integer" :
			     "string, bytes, or os.PathLike",
			     Py_TYPE(o)->tp_name);
		goto err;
	}

	length = PyBytes_GET_SIZE(bytes);
	tmp = PyBytes_AS_STRING(bytes);
	if ((size_t)length != strlen(tmp)) {
		PyErr_SetString(PyExc_TypeError,
				"path has embedded nul character");
		goto err;
	}

	path->path = tmp;
	if (bytes == o)
		Py_DECREF(bytes);
	else
		path->cleanup = bytes;
	path->fd = -1;

out:
	path->length = length;
	path->object = o;
	return Py_CLEANUP_SUPPORTED;

err:
	Py_XDECREF(o);
	Py_XDECREF(bytes);
	return 0;
}

void path_cleanup(struct path_arg *path)
{
	Py_CLEAR(path->object);
	Py_CLEAR(path->cleanup);
}

static PyMethodDef btrfsutil_methods[] = {
	{"sync", (PyCFunction)filesystem_sync,
	 METH_VARARGS | METH_KEYWORDS,
	 "sync(path)\n\n"
	 "Sync a specific Btrfs filesystem.\n\n"
	 "Arguments:\n"
	 "path -- string, bytes, path-like object, or open file descriptor"},
	{"start_sync", (PyCFunction)start_sync,
	 METH_VARARGS | METH_KEYWORDS,
	 "start_sync(path) -> int\n\n"
	 "Start a sync on a specific Btrfs filesystem and return the\n"
	 "transaction ID.\n\n"
	 "Arguments:\n"
	 "path -- string, bytes, path-like object, or open file descriptor"},
	{"wait_sync", (PyCFunction)wait_sync,
	 METH_VARARGS | METH_KEYWORDS,
	 "wait_sync(path, transid=0)\n\n"
	 "Wait for a transaction to sync.\n"
	 "Arguments:\n"
	 "path -- string, bytes, path-like object, or open file descriptor\n"
	 "transid -- int transaction ID to wait for, or zero for the current\n"
	 "transaction"},
	{},
};

static struct PyModuleDef btrfsutilmodule = {
	PyModuleDef_HEAD_INIT,
	"btrfsutil",
	"Library for managing Btrfs filesystems",
	-1,
	btrfsutil_methods,
};

PyMODINIT_FUNC
PyInit_btrfsutil(void)
{
	PyObject *m;

	BtrfsUtilError_type.tp_base = (PyTypeObject *)PyExc_OSError;
	if (PyType_Ready(&BtrfsUtilError_type) < 0)
		return NULL;

	QgroupInherit_type.tp_new = PyType_GenericNew;
	if (PyType_Ready(&QgroupInherit_type) < 0)
		return NULL;

	m = PyModule_Create(&btrfsutilmodule);
	if (!m)
		return NULL;

	Py_INCREF(&BtrfsUtilError_type);
	PyModule_AddObject(m, "BtrfsUtilError",
			   (PyObject *)&BtrfsUtilError_type);

	Py_INCREF(&QgroupInherit_type);
	PyModule_AddObject(m, "QgroupInherit",
			   (PyObject *)&QgroupInherit_type);

	add_module_constants(m);

	return m;
}