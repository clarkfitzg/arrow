// Convert between an Arrow array and an R vector

#include "Rarrow.h"

// From Rdefines - Got to be a better way
#undef length

using namespace arrow;


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
    int n = Rf_length(x);

    // Wes' code
    std::shared_ptr<MutableBuffer> data_buf;
    AllocateBuffer(default_memory_pool(), n * sizeof(double), &data_buf);
    memcpy(data_buf->mutable_data(), xr, n * sizeof(double));
    std::shared_ptr<DoubleArray> arr = std::make_shared<DoubleArray>(n, data_buf);

    // TODO: Pass destructor for array?
    SEXP r_ans = R_createRef(&arr, "arrow.array", NULL);
    return(r_ans);
}


// TODO: This is currently broken.
extern "C"
SEXP R_arrow_to_double(SEXP x)
{
    // Follow the pointer in the R object to the C object
    std::shared_ptr<DoubleArray>* array = GET_REF(x, std::shared_ptr<DoubleArray>);

	// Get the pointer to the actual data
	const double* data = (*array)->raw_values();

    int n = (*array)->length();

    SEXP out = PROTECT(allocVector(REALSXP, n));

    // C pointers to the actual data
    double* outp = REAL(out);

    // Using C std library to copy memory, Arrow may provide better way
    memcpy(outp, data, n * sizeof(double));

    UNPROTECT(1);
    return out;
}

