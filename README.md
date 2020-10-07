Introduction
============

ALTREP
------

ALTREP is a relatively new feature that has been released since R3.5. It
stands for alternative representation of R’s vector object. ALTREP is
capable to create an R vector with a customized data structure. The
difference between ALTREP and vector is opaque to R users. The main
motivation of the ALTREP is to reduce the memory load when creating some
special vector. Consider the example

    x <- 1:(1024*1024*1024*1024)
    length(x)
    #> [1] 1099511627776
    typeof(x)
    #> [1] "double"

You might think the above crazy code will exhaust all the memory in your
machine and burn your computer into ash. However, if you are using any
version of R that is equal or newer than 3.5, the code works like a
charm. You can access the data of `x` as usual

    head(x)
    #> [1] 1 2 3 4 5 6

You might have a guess on what has happened here. The data of the vector
`x` clearly has a lot of redundance. Since it is an arithmetic sequence,
you only need to know the first item and the common difference to
compute every values in `x`. As long as you do not need the entire data
at once, you do not have to put all data into your memory. A minimum
ALTREP object can be made by defining a length function and an element
retrieving function at C level, the function prototypes are

    R_xlen_t length(SEXP x);
    double real_elt(SEXP x, R_xlen_t i);

We wouldn’t go into details of the ALTREP but you can have an intuition
on how ALTREP works with these two functions. If you are interested in
making your own ALTREP, here are two great documents from the ancient
time

1.  [ALTREP and Other
    Things](https://www.r-project.org/dsc/2017/slides/dsc2017.pdf): A
    review of the structure of ALTREP
2.  [ALTREP and
    C++](https://purrple.cat/blog/2018/10/14/altrep-and-cpp/): A
    tutorial of ALTREP with examples. It helps me a lot when I first saw
    the idea of ALTREP

Challenge with ALTREP
---------------------

Although the idea of the ALTREP sounds exciting as it greatly extends
the flexibility of R’s vector, it breaks the assumption that all R’s
vectors have a pointer associated with them. While today’s R developers
might be aware of it(or not?), there has been many packages developed
before ALTREP and their work depends on this assumption. Before R3.5, It
is very common to loop over the data of R’s vector at C level like

    double my_sum(SEXP x){
      double* ptr = (double*)DATAPTR(x);
      double total = 0;
      for(int i = 0; i < XLENGTH(x); ++i){
        total = total + ptr[i];
      }
      return total;
    }

The above code works fine for a regular R vector since its data has been
allocated in the memory. However, for an ALTREP object, as it might not
have a pointer, it only has two options when `DATAPTR` is called:

1.  Allocating the data in memory and return the pointer of the data
2.  Call `Rf_error` to throw an error message.

While the first one seems to be a good choice, it is actually not always
feasible in practice for the object might be larger than the available
memory(As the crazy vector example we saw previously). The second choice
can be used if a pointer cannot be given, but it prevents the ALTREP
object from being used by many old but useful packages. Also, it might
causes memory leaking for the objects allocated in heap prior to the
call of `DATAPTR` may not have a chance to release themselves. For an
arithmetic sequence in the first example, it actually adopt both
strategies: It will allocate the memory for a short sequence, but throw
an error for a large sequence. For example

    ## Short sequence
    x1 <- 1:10
    x1[1] <- 10 
    head(x1)
    #> [1] 10  2  3  4  5  6

    ## long sequence 
    x2 <- 1:(1024*1024*1024*1024)
    x2[1] <- 10
    #> Error: cannot allocate vector of size 8192.0 Gb

As of R4.1, every attempt to change the values of an R vector will
require to access the pointer of the vector. For a short sequence this
can be easily solved via the memory allocation. However, for a large
sequence, since there would not be enough space for a 8192Gb vector,
doing so will end up with an error message. Requiring the pointer from
an ALTREP object has been a very serious limitation that prevents it
from being used in practice to represent a large data. That is the
problem that the Travel package is intended to solve.

Travel package
--------------

Travel package is an utility for developers to build ALTREP objects with
a virtual pointer. The pointer of the ALTREP object can be accessed via
the regular `DATAPTR` function in `Rinternals.h` at C level. The basic
workflow of using the package is

![](vignettes/Making%20altrep.png)

The pointer is “virtual” in the sense that the data does not exist in
the memory before one actually try to access the data. The pointer is
made via File mapping, but it wouldn’t consume any disk space neither
for the file being mapped is also a virtual file. All the request to
access the file will be sent to Travel callback functions and then
delivered to user provided data reading function. Suppose we have made
an ALTREP object with the data reading function `read_data`. Let the
pointer of the ALTREP object be `ptr`. Here is what happens behind the
scenes when you want to read the `i`th element of the pointer

![](vignettes/data%20request.png)

As we see from the flowchart, the data of the pointer `ptr` is made
on-demand. The pointer would not exhaust the memory even if it points to
an extremely large object. By doing that we solve the main limitation of
the ALTREP. The pointer of the ALTREP object can be accessed in a usual
way, and the memory consumption is minimum. Take the super large
sequence as an example again, the package provides a wrapper function to
turn an old ALTREP object into a new ALTREP object with a virtual
pointer.

    x <- 1:(1024*1024*1024*1024)
    y <- wrap_altrep(x)

    x[1:10]
    #>  [1]  1  2  3  4  5  6  7  8  9 10
    y[1:10]
    #>  [1]  1  2  3  4  5  6  7  8  9 10

While `x` and `y` looks the same, the pointer of `y` can be accessed as
usual

    x[1] <- 10
    #> Error: cannot allocate vector of size 8192.0 Gb
    x[1:10]
    #>  [1]  1  2  3  4  5  6  7  8  9 10

    y[1] <- 10
    y[1:10]
    #>  [1] 10  2  3  4  5  6  7  8  9 10

Furthermore, loop over the sequence `y` works as expected.

    ## We only compute the sum of the first 10 elements
    code <- 
    '
      double* ptr = (double*)DATAPTR(x);
      double total = 0;
        for(int i = 0; i < 10; ++i){
        total = total + ptr[i];
        }
      return ScalarReal(total);
    '
    my_sum <- cxxfunction(signature(x="SEXP"),
                        body=code)

    ## An error will be given for x
    my_sum(x)
    #> Error: cannot allocate vector of size 8192.0 Gb
    ## No error will be given and the sum can be computed
    my_sum(y)
    #> [1] 64

Please note that the wrapper function `wrap_altrep` should be used with
caution for it will call R’s function in a multithreaded environment. As
R is known to be a single-thread program, it is not recommended to use
this function in practice. `wrap_altrep` should be called for
demonstration purpose only. In the next section, we will show you how to
formally build your own ALTREP object using Travel package.

Travel tutorial
===============

Dependencies
------------

There are a few dependencies you need to install for using the package.

For *Windows*:

1.  [Dokan](https://dokan-dev.github.io/)

It is recommended to download `DokanSetup-noVC.exe` for this is the
library that the Travel package has been tested with.

For *Linux* and *Mac*:

1.  [fuse](https://github.com/libfuse/libfuse)
2.  [pkg-config]()

Link against Travel library
---------------------------

To avoid calling any R function when accessing the ALTREP data, you must
provide a C++ data reading function to the Travel package. Here is the
tutorial on how to include the Travel C++ header and link against its
static library.

### Step 1

For making the Travel header findable, add `Travel` to the `LinkingTo`
field of the DESCRIPTION file, e.g.

    LinkingTo: Travel

### Step 2

In your cpp files, include the header of the Travel functions
`#include "Travel/Travel.h"`.

### Step 3

To compile and link your package successfully against the `Travel` C++
library, you must include a `src/Makevars` file.

    TRAVEL_OBJECT_LIBS = $(shell echo 'Travel:::pkgconfig("PKG_LIBS")'|\
                         "${R_HOME}/bin/R" --vanilla --slave)
    TRAVEL_OBJECT_CPPFLAGS = $(shell echo 'Travel:::pkgconfig("PKG_CPPFLAGS")'|\
                             "${R_HOME}/bin/R" --vanilla --slave)
                             
    PKG_LIBS := $(PKG_LIBS) $(TRAVEL_OBJECT_LIBS)
    PKG_CPPFLAGS := $(PKG_CPPFLAGS) $(TRAVEL_OBJECT_CPPFLAGS)

Note that `$(shell ...)` is GNU make syntax so you should add GNU make
to the SystemRequirements field of the DESCRIPTION file of your package,
e.g.

    SystemRequirements: GNU make

You can find short explanations of the Travel C++ APIs in
`Travel/Travel.h`.

Use Travel
----------

The main function of the Travel package is `Travel_make_altptr`, its
function declaration is as follows

    SEXP Travel_make_altptr(int type, size_t length, file_data_func read_func, void *private_data, SEXP protect = R_NilValue);

where `type` specifies the R’s vector type defined in
`Rinternals.h`(e.g. `LGLSXP`, `INTSXP` or `REALSXP`), `length` is the
lenght of the vector. `read_func` is a function pointer that will
eventually be called when the data of the altrep is required.
`private_data` is a pointer for developers to store any data that is
opaque to the Travel package. `protect` is used to make sure the source
of the ALTREP(if any) will not be released before the ALTREP object is
released.

The function `read_func` is a core function to define an ALTREP object.
Its prototype is

    size_t (*file_data_func)(filesystem_file_data &file_data, void *buffer, size_t offset, size_t length);

where `file_data` is a simple struct which contains your `private_data`
pointer and many other member variables that are necessary for the
Travel package. `buffer` is the memory buffer that will hold the
requested data. `offset` is the index of the vector element that is
requested, note that the offset is an 0-based index. Therefore, if
`offset = 10` it means the `11`th element in the vector is requested.
`length` is the number of the vector elements that is requested starting
from `offset`. Each call to `read_func` will read a consecutive data in
the vector. The final read result should be written back to `buffer` and
the length of the read should be sent as the return value of the
function.

Besides the ALTREP creation function, the package also provides a smart
pointer to ease the development of the ALTREP. The prototypes of the
smart pointer are

    template <typename T>
    SEXP Travel_shared_ptr(T ptr, SEXP tag = R_NilValue, SEXP prot = R_NilValue);
    template <typename T>
    SEXP Travel_shared_ptr(T *ptr, SEXP tag = R_NilValue, SEXP prot = R_NilValue);

The return value of `Travel_shared_ptr` is R’s external pointer object
and the lifespan of the external pointer is controlled by R’s Garbage
collector. Once the external pointer is released, it will free the space
that is occupied by the pointer `ptr`. Here is two examples of using the
smart pointer

    SEXP extPtr = Travel_shared_ptr<int>(new int);
    SEXP extPtrArray = Travel_shared_ptr<int[]>(new int[10]);

The smart pointer can be used to release your `private_data` when the
ALTREP object is not in used and prevent it from memory leaking.
Combining all these functions together, we can make a simple arithmetic
sequence with any step value in R. Here are the code snippet for the
example. The full example can be found at
[TravelExample](https://github.com/Jiefei-Wang/TravelExample)

    #include <Rcpp.h>
    #include "Travel/Travel.h"
    using namespace Rcpp;

    struct Seq_info{
      size_t start;
      size_t step;
      ~Seq_info(){
        Rprintf("I am called from GC\n");
      }
    };

    // The data reading function
    size_t read_sequence(filesystem_file_data &file_data, void *buffer, size_t offset, size_t length)
    {
      Seq_info* info = (Seq_info*)file_data.private_data;
      for (size_t i = 0; i < length; i++)
      {
        ((double *)buffer)[i] = info -> start + info -> step * (offset + i);
      }
      return length;
    }

    // The main ALTREP making function
    // [[Rcpp::export]]
    SEXP make_sequence_altrep(size_t n, size_t start, size_t step)
    {
      Seq_info* info = new Seq_info{start, step};
      SEXP smart_ptr = Rf_protect(Travel_shared_ptr<Seq_info>(info));
      SEXP x = Rf_protect(Travel_make_altptr(REALSXP, n, read_sequence, info, smart_ptr));
      Rf_unprotect(2);
      return x;
    }

Here is the usage of the example package

    > x <- make_sequence_altrep(n = 1024*1024*1024*64, start = 1, step = 2)
    > length(x)
    [1] 68719476736
    > x[1:10]
     [1]  1  3  5  7  9 11 13 15 17 19
    > x[1] <- 100
    > x[1:10]
     [1] 100   3   5   7   9  11  13  15  17  19
    > 
    > rm(x)
    > gc()
    I am called from GC
              used (Mb) gc trigger (Mb) max used (Mb)
    Ncells  819800 43.8    1487236 79.5  1487236 79.5
    Vcells 1863208 14.3    8388608 64.0  2630899 20.1

Now you get a vector `x` with a crazy size, all operations of the ALTREP
can be supported. Enjoy the full power of the ALTREP objects!

Session info
============

    sessionInfo()
    #> R Under development (unstable) (2020-09-03 r79126)
    #> Platform: x86_64-w64-mingw32/x64 (64-bit)
    #> Running under: Windows 10 x64 (build 18362)
    #> 
    #> Matrix products: default
    #> 
    #> locale:
    #> [1] LC_COLLATE=English_United States.1252  LC_CTYPE=English_United States.1252   
    #> [3] LC_MONETARY=English_United States.1252 LC_NUMERIC=C                          
    #> [5] LC_TIME=English_United States.1252    
    #> 
    #> attached base packages:
    #> [1] stats     graphics  grDevices utils     datasets  methods   base     
    #> 
    #> other attached packages:
    #> [1] Travel_0.99.0  inline_0.3.16  testthat_2.3.2
    #> 
    #> loaded via a namespace (and not attached):
    #>  [1] Rcpp_1.0.5          rstudioapi_0.11     knitr_1.29          magrittr_1.5        pkgload_1.1.0      
    #>  [6] R6_2.4.1            rlang_0.4.7         fansi_0.4.1         stringr_1.4.0       tools_4.1.0        
    #> [11] pkgbuild_1.1.0      xfun_0.16           cli_2.0.2           withr_2.2.0         htmltools_0.5.0    
    #> [16] yaml_2.2.1          assertthat_0.2.1    digest_0.6.25       rprojroot_1.3-2     crayon_1.3.4       
    #> [21] processx_3.4.4      BiocManager_1.30.10 callr_3.4.3         ps_1.3.4            evaluate_0.14      
    #> [26] glue_1.4.2          rmarkdown_2.3       stringi_1.4.6       compiler_4.1.0      desc_1.2.0         
    #> [31] backports_1.1.9     prettyunits_1.1.1   BiocStyle_2.17.0