// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <Python.h>

import nom.example;

namespace
{
    PyObject* counter_value(PyObject*, PyObject* argument)
    {
        const Py_ssize_t count = PyLong_AsSsize_t(argument);
        if (count == -1 && PyErr_Occurred())
        {
            return nullptr;
        }
        if (count < 0)
        {
            PyErr_SetString(PyExc_ValueError, "count must be non-negative");
            return nullptr;
        }
        nom::example::counter counter;
        for (Py_ssize_t index = 0; index < count; ++index)
        {
            counter.increment();
        }
        return PyLong_FromLongLong(counter.value());
    }

    PyMethodDef methods[] = {
        {"counter_value", counter_value, METH_O, "Increment a C++ counter count times and return its value."},
        {nullptr, nullptr, 0, nullptr},
    };

    PyModuleDef module = {
        PyModuleDef_HEAD_INIT,
        "_native",
        "Bindings for nom.example.",
        -1,
        methods,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };
} // namespace

// CPython requires this exact exported symbol for the extension's module name.
PyMODINIT_FUNC PyInit__native() // NOLINT(readability-identifier-naming)
{
    return PyModule_Create(&module);
}
