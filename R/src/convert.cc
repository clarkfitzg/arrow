// Convert between an Arrow array and an R vector

#include <arrow/api.h>

#include <R.h>
#include <Rinternals.h>


#include <Rdefines.h>

// From Rrawpoppler
#define GET_REF(obj, type) \
  (type *) R_ExternalPtrAddr(GET_SLOT(obj, Rf_install("ref")))

SEXP createRef(void *ptr, const char * const classname, R_CFinalizer_t fin);

#define R_createRef(a, b, c)  createRef((a), (b), (c))



/* Example from Rrawpoppler
 *
extern "C"
SEXP R_Catalog_getPage(SEXP r_tthis, SEXP r_i)
{
    Catalog *tthis = GET_REF(r_tthis, Catalog);
    int i;
    i = INTEGER(r_i)[0];
    Page * ans;
    ans = tthis->getPage(i);
    SEXP r_ans = R_createRef(ans, "PagePtr", NULL);
    return(r_ans);
}
*/


extern "C"
SEXP double_to_arrow(SEXP x)
// Return a pointer to a newly created Arrow array
{

    // C pointers to the actual data
    double *xr = REAL(x);
    int n = length(x);

    std::shared_ptr<Int32Array> int32_array = std::static_pointer_cast<Int32Array>(array);

    SEXP r_ans = R_createRef(ans, "PagePtr", NULL);
    return(r_ans);
}


SEXP arrow_to_double

Status PandasObjectsToArrow(MemoryPool* pool, PyObject* ao, PyObject* mo,
    const std::shared_ptr<DataType>& type, std::shared_ptr<ChunkedArray>* out) {
  PandasConverter converter(pool, ao, mo, type);
  RETURN_NOT_OK(converter.ConvertObjects());
  *out = std::make_shared<ChunkedArray>(converter.result());
  return Status::OK();
}
