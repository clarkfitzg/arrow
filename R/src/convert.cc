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


// TODO: This is currently totally broken.
extern "C"
SEXP R_arrow_to_double(SEXP x)
{
    // Follow the pointer in the R object to the C object
    //Catalog *tthis = GET_REF(r_tthis, Catalog);
    std::shared_ptr<arrow::Array>* array = GET_REF(x, std::shared_ptr<arrow::Array>);

    // Following "Getting started"
	// Cast the Array to its actual type to access its data
	std::shared_ptr<arrow::DoubleArray> arr = std::static_pointer_cast<arrow::DoubleArray>(*array);

	// Get the pointer to the null bitmap.
	//const uint8_t* null_bitmap = int64_array->null_bitmap_data();

	// Get the pointer to the actual data
	const double* data = arr->raw_values();

    // TODO: Call arrow method
    int n = 3;

    SEXP out = PROTECT(allocVector(REALSXP, n));

    // C pointers to the actual data
    double* outp = REAL(out);

    // Using C std library to copy memory, Arrow may provide better way
    memcpy(outp, data, n * sizeof(double));

    UNPROTECT(1);
    return out;
}

