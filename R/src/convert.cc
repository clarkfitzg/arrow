// Convert between an Arrow array and an R vector

#include "Rarrow.h"


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
    SEXP r_ans = R_createRef(&array, "arrow.array", NULL);
    return(r_ans);
}


extern "C"
SEXP R_arrow_to_double(SEXP x)
{
}
