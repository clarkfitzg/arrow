#include <arrow/api.h>

#include <R.h>
#include <Rinternals.h>
#include <Rdefines.h>


// From Rrawpoppler
//
// This stuff will go into a header file
#define GET_REF(obj, type) \
  (type *) R_ExternalPtrAddr(GET_SLOT(obj, Rf_install("ref")))

// Where is the implementation of this?
SEXP createRef(void *ptr, const char * const classname, R_CFinalizer_t fin);

#define R_createRef(a, b, c)  createRef((a), (b), (c))


SEXP
createRef(void *ptr, const char * const classname, R_CFinalizer_t fin)
{
  SEXP robj, klass, ref;
 
  PROTECT(klass = MAKE_CLASS(classname));
  PROTECT(robj = NEW(klass));
  SET_SLOT(robj, Rf_install("ref"), ref = R_MakeExternalPtr(ptr, Rf_install(classname), R_NilValue));

  // Set finalizer to garbage collect when we let go/release this object.
  if(fin)
     R_RegisterCFinalizer(ref, fin);
  UNPROTECT(2);  
  return(robj);
}
