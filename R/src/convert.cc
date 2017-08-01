// Convert between an Arrow array and an R vector

#include <arrow/api.h>

#include <R.h>
#include <Rinternals.h>


#include <Rdefines.h>

// From Rrawpoppler
//
// This stuff will go into a header file
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
SEXP R_double_to_arrow(SEXP x)
// Return a pointer to a newly created Arrow array
{

    // C pointers to the actual data
    double *xr = REAL(x);
    int n = length(x);

    // Following "Getting started"
    arrow::DoubleBuilder builder(arrow::default_memory_pool(), arrow::float64());

    for (int i = 0; i < n; ++i) {
        builder.Append(xr[i]);
    }

    std::shared_ptr<arrow::Array> array;
    builder.Finish(&array);

    // TODO: Pass destructor for array?
    // A pointer to a shared pointer. Terrible idea?
    SEXP r_ans = R_createRef(&array, "arrow::array", NULL);
    return(r_ans);
}
