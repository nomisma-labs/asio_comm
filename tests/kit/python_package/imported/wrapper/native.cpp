// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <Python.h>

extern "C" int wheel_provider_answer();

namespace
{
    PyObject* answer(PyObject*, PyObject*)
    {
        return PyLong_FromLong(wheel_provider_answer());
    }

    PyMethodDef methods[] = {
        {"answer", answer, METH_NOARGS, "Return a value from the separately installed shared library."},
        {nullptr, nullptr, 0, nullptr},
    };

    PyModuleDef native_module = {
        PyModuleDef_HEAD_INIT,
        "_native",
        nullptr,
        -1,
        methods,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };
} // namespace

PyMODINIT_FUNC PyInit__native()
{
    return PyModule_Create(&native_module);
}
