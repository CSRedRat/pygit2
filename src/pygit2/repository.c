/*
 * Copyright 2010-2012 The pygit2 contributors
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2,
 * as published by the Free Software Foundation.
 *
 * In addition to the permissions in the GNU General Public License,
 * the authors give you unlimited permission to link the compiled
 * version of this file into combinations with other programs,
 * and to distribute those combinations without any restriction
 * coming from the use of this file.  (The General Public License
 * restrictions do apply in other respects; for example, they cover
 * modification of the file, and distribution when not linked into
 * a combined executable.)
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <pygit2/error.h>
#include <pygit2/types.h>
#include <pygit2/reference.h>
#include <pygit2/utils.h>
#include <pygit2/object.h>
#include <pygit2/oid.h>
#include <pygit2/repository.h>

extern PyObject *GitError;

extern PyTypeObject IndexType;
extern PyTypeObject WalkerType;
extern PyTypeObject SignatureType;
extern PyTypeObject TreeType;
extern PyTypeObject TreeBuilderType;
extern PyTypeObject ConfigType;
extern PyTypeObject DiffType;

git_otype
int_to_loose_object_type(int type_id)
{
    switch((git_otype)type_id) {
        case GIT_OBJ_COMMIT: return GIT_OBJ_COMMIT;
        case GIT_OBJ_TREE: return GIT_OBJ_TREE;
        case GIT_OBJ_BLOB: return GIT_OBJ_BLOB;
        case GIT_OBJ_TAG: return GIT_OBJ_TAG;
        default: return GIT_OBJ_BAD;
    }
}

PyObject *
lookup_object_prefix(Repository *repo, const git_oid *oid, size_t len,
                     git_otype type)
{
    int err;
    git_object *obj;

    err = git_object_lookup_prefix(&obj, repo->repo, oid,
                                   (unsigned int)len, type);
    if (err < 0)
        return Error_set_oid(err, oid, len);

    return wrap_object(obj, repo);
}

PyObject *
lookup_object(Repository *repo, const git_oid *oid, git_otype type)
{
    return lookup_object_prefix(repo, oid, GIT_OID_HEXSZ, type);
}

int
Repository_init(Repository *self, PyObject *args, PyObject *kwds)
{
    char *path;
    int err;

    if (kwds) {
        PyErr_SetString(PyExc_TypeError,
                        "Repository takes no keyword arguments");
        return -1;
    }

    if (!PyArg_ParseTuple(args, "s", &path))
        return -1;

    err = git_repository_open(&self->repo, path);
    if (err < 0) {
        Error_set_str(err, path);
        return -1;
    }

    return 0;
}

void
Repository_dealloc(Repository *self)
{
    PyObject_GC_UnTrack(self);
    Py_XDECREF(self->index);
    git_repository_free(self->repo);
    PyObject_GC_Del(self);
}

int
Repository_traverse(Repository *self, visitproc visit, void *arg)
{
    Py_VISIT(self->index);
    return 0;
}

int
Repository_clear(Repository *self)
{
    Py_CLEAR(self->index);
    return 0;
}

int
Repository_contains(Repository *self, PyObject *value)
{
    git_oid oid;
    git_odb *odb;
    int err, len, exists;

    len = py_str_to_git_oid(value, &oid);
    if (len < 0)
        return -1;

    err = git_repository_odb(&odb, self->repo);
    if (err < 0) {
        Error_set(err);
        return -1;
    }

    if (len < GIT_OID_HEXSZ) {
        git_odb_object *obj = NULL;
        err = git_odb_read_prefix(&obj, odb, &oid, len);
        if (err < 0 && err != GIT_ENOTFOUND) {
            Error_set(err);
            exists = -1;
        } else {
            exists = (err == 0);
            if (obj)
                git_odb_object_free(obj);
        }
    } else {
        exists = git_odb_exists(odb, &oid);
    }

    git_odb_free(odb);
    return exists;
}

static int
Repository_build_as_iter(const git_oid *oid, void *accum)
{
    int err;
    PyObject *oid_str = git_oid_to_py_str(oid);

    err = PyList_Append((PyObject*)accum, oid_str);
    Py_DECREF(oid_str);
    return err;
}

PyObject *
Repository_as_iter(Repository *self)
{
    git_odb *odb;
    int err;
    PyObject *accum = PyList_New(0);

    err = git_repository_odb(&odb, self->repo);
    if (err < 0)
        return Error_set(err);

    err = git_odb_foreach(odb, Repository_build_as_iter, (void*)accum);
    git_odb_free(odb);
    if (err == GIT_EUSER) {
        return NULL;
    } else if (err < 0) {
        return Error_set(err);
    }

    return PyObject_GetIter(accum);
}


PyDoc_STRVAR(Repository_head__doc__,
  "Current head reference of the repository.");

PyObject *
Repository_get_head(Repository *self)
{
    git_reference *head;
    const git_oid *oid;
    PyObject *pyobj;
    int err;

    err = git_repository_head(&head, self->repo);
    if(err < 0) {
      if(err == GIT_ENOTFOUND)
        PyErr_SetString(GitError, "head reference does not exist");
      else
        Error_set(err);

      return NULL;
    }

    oid = git_reference_target(head);
    pyobj = lookup_object(self, oid, GIT_OBJ_COMMIT);
    git_reference_free(head);
    return pyobj;
}


PyDoc_STRVAR(Repository_head_is_detached__doc__,
  "A repository's HEAD is detached when it points directly to a commit "
  "instead of a branch.");

PyObject *
Repository_head_is_detached(Repository *self)
{
    if (git_repository_head_detached(self->repo) > 0)
        Py_RETURN_TRUE;

    Py_RETURN_FALSE;
}


PyDoc_STRVAR(Repository_head_is_orphaned__doc__,
  "An orphan branch is one named from HEAD but which doesn't exist in the "
  "refs namespace, because it doesn't have any commit to point to.");

PyObject *
Repository_head_is_orphaned(Repository *self)
{
    if (git_repository_head_orphan(self->repo) > 0)
        Py_RETURN_TRUE;

    Py_RETURN_FALSE;
}


PyDoc_STRVAR(Repository_is_empty__doc__,
  "Check if a repository is empty.");

PyObject *
Repository_is_empty(Repository *self)
{
    if (git_repository_is_empty(self->repo) > 0)
        Py_RETURN_TRUE;

    Py_RETURN_FALSE;
}


PyDoc_STRVAR(Repository_is_bare__doc__,
  "Check if a repository is a bare repository.");

PyObject *
Repository_is_bare(Repository *self)
{
    if (git_repository_is_bare(self->repo) > 0)
        Py_RETURN_TRUE;

    Py_RETURN_FALSE;
}


PyObject *
Repository_getitem(Repository *self, PyObject *value)
{
    git_oid oid;
    int len;

    len = py_str_to_git_oid(value, &oid);
    if (len < 0)
        return NULL;

    return lookup_object_prefix(self, &oid, len, GIT_OBJ_ANY);
}

PyObject *
Repository_revparse_single(Repository *self, PyObject *py_spec)
{
    git_object *c_obj;
    char *c_spec;
    char *encoding = "ascii";
    int err;

    /* 1- Get the C revision spec */
    c_spec = py_str_to_c_str(py_spec, encoding);
    if (c_spec == NULL)
        return NULL;

    /* 2- Lookup */
    err = git_revparse_single(&c_obj, self->repo, c_spec);

    if (err < 0) {
        PyObject *err_obj = Error_set_str(err, c_spec);
        free(c_spec);
        return err_obj;
    }
    free(c_spec);

    return wrap_object(c_obj, self);
}

git_odb_object *
Repository_read_raw(git_repository *repo, const git_oid *oid, size_t len)
{
    git_odb *odb;
    git_odb_object *obj;
    int err;

    err = git_repository_odb(&odb, repo);
    if (err < 0) {
        Error_set(err);
        return NULL;
    }

    err = git_odb_read_prefix(&obj, odb, oid, (unsigned int)len);
    git_odb_free(odb);
    if (err < 0) {
        Error_set_oid(err, oid, len);
        return NULL;
    }

    return obj;
}


PyDoc_STRVAR(Repository_read__doc__,
  "read(oid) -> type, data, size\n\n"
  "Read raw object data from the repository.");

PyObject *
Repository_read(Repository *self, PyObject *py_hex)
{
    git_oid oid;
    git_odb_object *obj;
    int len;
    PyObject* tuple;

    len = py_str_to_git_oid(py_hex, &oid);
    if (len < 0)
        return NULL;

    obj = Repository_read_raw(self->repo, &oid, len);
    if (obj == NULL)
        return NULL;

    tuple = Py_BuildValue(
        "(ns#)",
        git_odb_object_type(obj),
        git_odb_object_data(obj),
        git_odb_object_size(obj));

    git_odb_object_free(obj);
    return tuple;
}


PyDoc_STRVAR(Repository_write__doc__,
  "write(type, data) -> oid\n\n"
  "Write raw object data into the repository. First arg is the object "
  "type, the second one a buffer with data. Return the object id (sha) "
  "of the created object.");

PyObject *
Repository_write(Repository *self, PyObject *args)
{
    int err;
    git_oid oid;
    git_odb *odb;
    git_odb_stream* stream;
    int type_id;
    const char* buffer;
    Py_ssize_t buflen;
    git_otype type;

    if (!PyArg_ParseTuple(args, "Is#", &type_id, &buffer, &buflen))
        return NULL;

    type = int_to_loose_object_type(type_id);
    if (type == GIT_OBJ_BAD)
        return PyErr_Format(PyExc_ValueError, "%d", type_id);

    err = git_repository_odb(&odb, self->repo);
    if (err < 0)
        return Error_set(err);

    err = git_odb_open_wstream(&stream, odb, buflen, type);
    git_odb_free(odb);
    if (err < 0)
        return Error_set(err);

    stream->write(stream, buffer, buflen);
    err = stream->finalize_write(&oid, stream);
    stream->free(stream);
    return git_oid_to_python(oid.id);
}


PyDoc_STRVAR(Repository_index__doc__, "Index file.");

PyObject *
Repository_get_index(Repository *self, void *closure)
{
    int err;
    git_index *index;
    Index *py_index;

    assert(self->repo);

    if (self->index == NULL) {
        err = git_repository_index(&index, self->repo);
        if (err < 0)
            return Error_set(err);

        py_index = PyObject_GC_New(Index, &IndexType);
        if (!py_index) {
            git_index_free(index);
            return NULL;
        }

        Py_INCREF(self);
        py_index->repo = self;
        py_index->index = index;
        PyObject_GC_Track(py_index);
        self->index = (PyObject*)py_index;
    }

    Py_INCREF(self->index);
    return self->index;
}


PyDoc_STRVAR(Repository_path__doc__,
  "The normalized path to the git repository.");

PyObject *
Repository_get_path(Repository *self, void *closure)
{
    return to_path(git_repository_path(self->repo));
}


PyDoc_STRVAR(Repository_workdir__doc__,
  "The normalized path to the working directory of the repository. "
  "If the repository is bare, None will be returned.");

PyObject *
Repository_get_workdir(Repository *self, void *closure)
{
    const char *c_path;

    c_path = git_repository_workdir(self->repo);
    if (c_path == NULL)
        Py_RETURN_NONE;

    return to_path(c_path);
}


PyDoc_STRVAR(Repository_config__doc__,
  "Get the configuration file for this repository.\n\n"
  "If a configuration file has not been set, the default config set for the "
  "repository will be returned, including global and system configurations "
  "(if they are available).");

PyObject *
Repository_get_config(Repository *self, void *closure)
{
    int err;
    git_config *config;
    Config *py_config;

    assert(self->repo);

    if (self->config == NULL) {
        err = git_repository_config(&config, self->repo);
        if (err < 0)
            return Error_set(err);

        py_config = PyObject_GC_New(Config, &ConfigType);
        if (!py_config) {
            git_config_free(config);
            return NULL;
        }

        Py_INCREF(self);
        py_config->repo = self;
        py_config->config = config;
        PyObject_GC_Track(py_config);
        self->config = (PyObject*)py_config;
    }

    Py_INCREF(self->config);
    return self->config;
}


PyDoc_STRVAR(Repository_walk__doc__,
  "walk(oid, sort_mode) -> iterator\n\n"
  "Generator that traverses the history starting from the given commit.");

PyObject *
Repository_walk(Repository *self, PyObject *args)
{
    PyObject *value;
    unsigned int sort;
    int err;
    git_oid oid;
    git_revwalk *walk;
    Walker *py_walker;

    if (!PyArg_ParseTuple(args, "OI", &value, &sort))
        return NULL;

    err = git_revwalk_new(&walk, self->repo);
    if (err < 0)
        return Error_set(err);

    /* Sort */
    git_revwalk_sorting(walk, sort);

    /* Push */
    if (value != Py_None) {
        err = py_str_to_git_oid_expand(self->repo, value, &oid);
        if (err < 0) {
            git_revwalk_free(walk);
            return Error_set(err);
        }

        err = git_revwalk_push(walk, &oid);
        if (err < 0) {
            git_revwalk_free(walk);
            return Error_set(err);
        }
    }

    py_walker = PyObject_New(Walker, &WalkerType);
    if (!py_walker) {
        git_revwalk_free(walk);
        return NULL;
    }

    Py_INCREF(self);
    py_walker->repo = self;
    py_walker->walk = walk;
    return (PyObject*)py_walker;
}


PyObject *
Repository_create_blob(Repository *self, PyObject *args)
{
    git_oid oid;
    const char* raw;
    Py_ssize_t size;
    int err;

    if (!PyArg_ParseTuple(args, "s#", &raw, &size))
      return NULL;

    err = git_blob_create_frombuffer(&oid, self->repo, (const void*)raw, size);

    if (err < 0)
      return Error_set(err);

    return git_oid_to_python(oid.id);
}

PyObject *
Repository_create_blob_fromfile(Repository *self, PyObject *args)
{
    git_oid oid;
    const char* path;
    int err;

    if (!PyArg_ParseTuple(args, "s", &path))
      return NULL;

    err = git_blob_create_fromworkdir(&oid, self->repo, path);

    if (err < 0)
      return Error_set(err);

    return git_oid_to_python(oid.id);
}


PyDoc_STRVAR(Repository_create_commit__doc__,
  "create_commit(reference, author, committer, message, tree, parents"
  "[, encoding]) -> bytes\n\n"
  "Create a new commit object, return its SHA.");

PyObject *
Repository_create_commit(Repository *self, PyObject *args)
{
    Signature *py_author, *py_committer;
    PyObject *py_oid, *py_message, *py_parents, *py_parent;
    PyObject *py_result = NULL;
    char *message = NULL;
    char *update_ref = NULL;
    char *encoding = NULL;
    git_oid oid;
    git_tree *tree = NULL;
    int parent_count;
    git_commit **parents = NULL;
    int err = 0, i = 0, len;

    if (!PyArg_ParseTuple(args, "zO!O!OOO!|s",
                          &update_ref,
                          &SignatureType, &py_author,
                          &SignatureType, &py_committer,
                          &py_message,
                          &py_oid,
                          &PyList_Type, &py_parents,
                          &encoding))
        return NULL;

    len = py_str_to_git_oid(py_oid, &oid);
    if (len < 0)
        goto out;

    message = py_str_to_c_str(py_message, encoding);
    if (message == NULL)
        goto out;

    err = git_tree_lookup_prefix(&tree, self->repo, &oid, (unsigned int)len);
    if (err < 0) {
        Error_set(err);
        goto out;
    }

    parent_count = (int)PyList_Size(py_parents);
    parents = malloc(parent_count * sizeof(git_commit*));
    if (parents == NULL) {
        PyErr_SetNone(PyExc_MemoryError);
        goto out;
    }
    for (; i < parent_count; i++) {
        py_parent = PyList_GET_ITEM(py_parents, i);
        len = py_str_to_git_oid(py_parent, &oid);
        if (len < 0)
            goto out;
        if (git_commit_lookup_prefix(&parents[i], self->repo, &oid,
                                     (unsigned int)len))
            goto out;
    }

    err = git_commit_create(&oid, self->repo, update_ref,
                            py_author->signature, py_committer->signature,
                            encoding, message, tree, parent_count,
                            (const git_commit**)parents);
    if (err < 0) {
        Error_set(err);
        goto out;
    }

    py_result = git_oid_to_python(oid.id);

out:
    free(message);
    git_tree_free(tree);
    while (i > 0) {
        i--;
        git_commit_free(parents[i]);
    }
    free(parents);
    return py_result;
}


PyDoc_STRVAR(Repository_create_tag__doc__,
  "create_tag(name, oid, type, tagger, message) -> bytes\n\n"
  "Create a new tag object, return its SHA.");

PyObject *
Repository_create_tag(Repository *self, PyObject *args)
{
    PyObject *py_oid;
    Signature *py_tagger;
    char *tag_name, *message;
    git_oid oid;
    git_object *target = NULL;
    int err, target_type, len;

    if (!PyArg_ParseTuple(args, "sOiO!s",
                          &tag_name,
                          &py_oid,
                          &target_type,
                          &SignatureType, &py_tagger,
                          &message))
        return NULL;

    len = py_str_to_git_oid(py_oid, &oid);
    if (len < 0)
        return NULL;

    err = git_object_lookup_prefix(&target, self->repo, &oid,
                                   (unsigned int)len, target_type);
    err = err < 0 ? err : git_tag_create(&oid, self->repo, tag_name, target,
                         py_tagger->signature, message, 0);
    git_object_free(target);
    if (err < 0)
        return Error_set_oid(err, &oid, len);
    return git_oid_to_python(oid.id);
}

PyObject *
Repository_listall_references(Repository *self, PyObject *args)
{
    unsigned list_flags=GIT_REF_LISTALL;
    git_strarray c_result;
    PyObject *py_result, *py_string;
    unsigned index;
    int err;

    /* 1- Get list_flags */
    if (!PyArg_ParseTuple(args, "|I", &list_flags))
        return NULL;

    /* 2- Get the C result */
    err = git_reference_list(&c_result, self->repo, list_flags);
    if (err < 0)
        return Error_set(err);

    /* 3- Create a new PyTuple */
    py_result = PyTuple_New(c_result.count);
    if (py_result == NULL)
        goto out;

    /* 4- Fill it */
    for (index=0; index < c_result.count; index++) {
        py_string = to_path((c_result.strings)[index]);
        if (py_string == NULL) {
            Py_CLEAR(py_result);
            goto out;
        }
        PyTuple_SET_ITEM(py_result, index, py_string);
    }

out:
    git_strarray_free(&c_result);
    return py_result;
}

PyObject *
Repository_lookup_reference(Repository *self, PyObject *py_name)
{
    git_reference *c_reference;
    char *c_name;
    int err;

    /* 1- Get the C name */
    c_name = py_path_to_c_str(py_name);
    if (c_name == NULL)
        return NULL;

    /* 2- Lookup */
    err = git_reference_lookup(&c_reference, self->repo, c_name);
    if (err < 0) {
        PyObject *err_obj = Error_set_str(err, c_name);
        free(c_name);
        return err_obj;
    }
    free(c_name);
    /* 3- Make an instance of Reference and return it */
    return wrap_reference(c_reference);
}

PyDoc_STRVAR(
  Repository_create_reference_doc,
  "Create a new reference \"name\" which points to a object or another reference\n\n"
  "Arguments: (name, source, force=False, symbolic=False)\n\n"
  "With force=True references will be overridden. Otherwise an exception is raised"
  "You can create either a normal reference or a symbolic one:\n"
  "  * normal reference: source has to be a valid sha hash\n"
  "  * symbolic reference: source has to be a valid existing reference name\n\n"
  "Examples:\n"
  "   repo.create_reference('refs/heads/foo', repo.head.hex)\n"
  "   repo.create_reference('refs/tags/foo', 'refs/heads/master', symbolic = True)\n"
);

PyObject *
Repository_create_reference(Repository *self,  PyObject *args, PyObject* keywds)
{
    PyObject *py_obj;
    git_reference *c_reference;
    char *c_name, *c_target;
    git_oid oid;
    int err = 0, symbolic = 0, force = 0;

    static char *kwlist[] = {"name", "source", "force", "symbolic", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "sO|ii", kwlist,
                                     &c_name, &py_obj, &force, &symbolic))
        return NULL;

    if(!symbolic) {
        err = py_str_to_git_oid_expand(self->repo, py_obj, &oid);
        if (err < 0) {
            return Error_set(err);
        }

        err = git_reference_create(&c_reference, self->repo, c_name, &oid, force);
    } else {
        #if PY_MAJOR_VERSION == 2
        c_target = PyString_AsString(py_obj);
        #else
        c_target = PyString_AsString(PyUnicode_AsASCIIString(py_obj));
        #endif
        if(c_target == NULL)
            return NULL;

        err = git_reference_symbolic_create(&c_reference, self->repo, c_name,
                                            c_target, force);
    }

    if (err < 0)
        return Error_set(err);

    return wrap_reference(c_reference);
}


PyObject *
Repository_packall_references(Repository *self,  PyObject *args)
{
    int err;

    /* 1- Pack */
    err = git_reference_packall(self->repo);
    if (err < 0)
        return Error_set(err);

    /* 2- Return None */
    Py_RETURN_NONE;
}

int
read_status_cb(const char *path, unsigned int status_flags, void *payload)
{
    /* This is the callback that will be called in git_status_foreach. It
     * will be called for every path.*/
    PyObject *flags;

    flags = PyInt_FromLong((long) status_flags);
    PyDict_SetItemString(payload, path, flags);

    return GIT_OK;
}

PyObject *
Repository_status(Repository *self, PyObject *args)
{
    PyObject *payload_dict;

    payload_dict = PyDict_New();
    git_status_foreach(self->repo, read_status_cb, payload_dict);

    return payload_dict;
}

PyObject *
Repository_status_file(Repository *self, PyObject *value)
{
    char *path;
    unsigned int status;
    int err;

    path = py_path_to_c_str(value);
    if (!path)
        return NULL;

    err = git_status_file(&status, self->repo, path);
    if (err < 0) {
        PyObject *err_obj =  Error_set_str(err, path);
        free(path);
        return err_obj;
    }
    return PyInt_FromLong(status);
}

PyObject *
Repository_TreeBuilder(Repository *self, PyObject *args)
{
    TreeBuilder *builder;
    git_treebuilder *bld;
    PyObject *py_src = NULL;
    git_oid oid;
    git_tree *tree = NULL;
    git_tree *must_free = NULL;
    int err;

    if (!PyArg_ParseTuple(args, "|O", &py_src))
        return NULL;

    if (py_src) {
        if (PyObject_TypeCheck(py_src, &TreeType)) {
            Tree *py_tree = (Tree *)py_src;
            if (py_tree->repo->repo != self->repo) {
                //return Error_set(GIT_EINVALIDARGS);
                return Error_set(GIT_ERROR);
            }
            tree = py_tree->tree;
        } else {
            err = py_str_to_git_oid_expand(self->repo, py_src, &oid);
            if (err < 0)
                return NULL;

            err = git_tree_lookup(&tree, self->repo, &oid);
            if (err < 0)
                return Error_set(err);
            must_free = tree;
        }
    }

    err = git_treebuilder_create(&bld, tree);
    if (must_free != NULL) {
        git_tree_free(must_free);
    }

    if (err < 0)
        return Error_set(err);

    builder = PyObject_New(TreeBuilder, &TreeBuilderType);
    if (builder) {
        builder->repo = self;
        builder->bld = bld;
        Py_INCREF(self);
    }

    return (PyObject*)builder;
}

PyMethodDef Repository_methods[] = {
    {"create_commit", (PyCFunction)Repository_create_commit, METH_VARARGS,
     Repository_create_commit__doc__},
    {"create_tag", (PyCFunction)Repository_create_tag, METH_VARARGS,
     Repository_create_tag__doc__},
    {"walk", (PyCFunction)Repository_walk, METH_VARARGS,
     Repository_walk__doc__},
    {"read", (PyCFunction)Repository_read, METH_O, Repository_read__doc__},
    {"write", (PyCFunction)Repository_write, METH_VARARGS,
     Repository_write__doc__},
    {"listall_references", (PyCFunction)Repository_listall_references,
      METH_VARARGS,
      "Return a list with all the references in the repository."},
    {"lookup_reference", (PyCFunction)Repository_lookup_reference, METH_O,
       "Lookup a reference by its name in a repository."},
    {"revparse_single", (PyCFunction)Repository_revparse_single, METH_O,
     "Find an object, as specified by a revision string. See "
     "`man gitrevisions`, or the documentation for `git rev-parse` for "
     "information on the syntax accepted."},
    {"create_blob", (PyCFunction)Repository_create_blob,
     METH_VARARGS,
     "Create a new blob from memory"},
    {"create_blob_fromfile", (PyCFunction)Repository_create_blob_fromfile,
     METH_VARARGS,
     "Create a new blob from file"},
    {"create_reference", (PyCFunction)Repository_create_reference,
     METH_VARARGS|METH_KEYWORDS,
     Repository_create_reference_doc},
    {"packall_references", (PyCFunction)Repository_packall_references,
     METH_NOARGS, "Pack all the loose references in the repository."},
    {"status", (PyCFunction)Repository_status, METH_NOARGS, "Reads the "
     "status of the repository and returns a dictionary with file paths "
     "as keys and status flags as values.\nSee pygit2.GIT_STATUS_*."},
    {"status_file", (PyCFunction)Repository_status_file, METH_O,
     "Returns the status of the given file path."},
    {"TreeBuilder", (PyCFunction)Repository_TreeBuilder, METH_VARARGS,
     "Create a TreeBuilder object for this repository."},
    {NULL}
};

PyGetSetDef Repository_getseters[] = {
    {"index", (getter)Repository_get_index, NULL, Repository_index__doc__,
     NULL},
    {"path", (getter)Repository_get_path, NULL, Repository_path__doc__, NULL},
    {"head", (getter)Repository_get_head, NULL, Repository_head__doc__, NULL},
    {"head_is_detached", (getter)Repository_head_is_detached, NULL,
     Repository_head_is_detached__doc__},
    {"head_is_orphaned", (getter)Repository_head_is_orphaned, NULL,
     Repository_head_is_orphaned__doc__},
    {"is_empty", (getter)Repository_is_empty, NULL,
     Repository_is_empty__doc__},
    {"is_bare", (getter)Repository_is_bare, NULL, Repository_is_bare__doc__},
    {"config", (getter)Repository_get_config, NULL, Repository_config__doc__,
     NULL},
    {"workdir", (getter)Repository_get_workdir, NULL,
     Repository_workdir__doc__, NULL},
    {NULL}
};

PySequenceMethods Repository_as_sequence = {
    0,                               /* sq_length */
    0,                               /* sq_concat */
    0,                               /* sq_repeat */
    0,                               /* sq_item */
    0,                               /* sq_slice */
    0,                               /* sq_ass_item */
    0,                               /* sq_ass_slice */
    (objobjproc)Repository_contains, /* sq_contains */
};

PyMappingMethods Repository_as_mapping = {
    0,                               /* mp_length */
    (binaryfunc)Repository_getitem,  /* mp_subscript */
    0,                               /* mp_ass_subscript */
};


PyDoc_STRVAR(Repository__doc__,
    "Repository(path) -> Repository\n\n"
    "Git repository.");

PyTypeObject RepositoryType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_pygit2.Repository",                       /* tp_name           */
    sizeof(Repository),                        /* tp_basicsize      */
    0,                                         /* tp_itemsize       */
    (destructor)Repository_dealloc,            /* tp_dealloc        */
    0,                                         /* tp_print          */
    0,                                         /* tp_getattr        */
    0,                                         /* tp_setattr        */
    0,                                         /* tp_compare        */
    0,                                         /* tp_repr           */
    0,                                         /* tp_as_number      */
    &Repository_as_sequence,                   /* tp_as_sequence    */
    &Repository_as_mapping,                    /* tp_as_mapping     */
    0,                                         /* tp_hash           */
    0,                                         /* tp_call           */
    0,                                         /* tp_str            */
    0,                                         /* tp_getattro       */
    0,                                         /* tp_setattro       */
    0,                                         /* tp_as_buffer      */
    Py_TPFLAGS_DEFAULT |
    Py_TPFLAGS_BASETYPE |
    Py_TPFLAGS_HAVE_GC,                        /* tp_flags          */
    Repository__doc__,                         /* tp_doc            */
    (traverseproc)Repository_traverse,         /* tp_traverse       */
    (inquiry)Repository_clear,                 /* tp_clear          */
    0,                                         /* tp_richcompare    */
    0,                                         /* tp_weaklistoffset */
    (getiterfunc)Repository_as_iter,           /* tp_iter           */
    0,                                         /* tp_iternext       */
    Repository_methods,                        /* tp_methods        */
    0,                                         /* tp_members        */
    Repository_getseters,                      /* tp_getset         */
    0,                                         /* tp_base           */
    0,                                         /* tp_dict           */
    0,                                         /* tp_descr_get      */
    0,                                         /* tp_descr_set      */
    0,                                         /* tp_dictoffset     */
    (initproc)Repository_init,                 /* tp_init           */
    0,                                         /* tp_alloc          */
    0,                                         /* tp_new            */
};
