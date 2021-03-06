context("Testing filesystem reading feature with altrep")
set_verbose(FALSE)
rm(list=ls())
gc()
deploy_filesystem()


n <- 1024*1024*16/4
block_size <- 1024
block_num <- n/block_size
y <- 0:(n-1)

test_that("Create an integer sequece",{
    expect_error(x <- C_make_int_sequence_altrep(n),NA)
    expect_true(length(x)==n)
    expect_error({rm(x);gc()},NA)
})

test_that("sequential access an integer sequece",{
    expect_error(x <- C_make_int_sequence_altrep(n),NA)
    
    for(i in c(seq_len(10), block_num-10 + seq_len(10))){
        ind <- 1:block_size + (i-1)*block_size
        expect_equal(x[ind], y[ind])
    }
})

test_that("random access an integer sequece",{
    expect_error(x <- C_make_int_sequence_altrep(n),NA)
    for(i in seq_len(10)){
        ind <- sample(1:n, block_size)
        expect_equal(x[ind], y[ind])
    }
})

gc()

test_that("Check virtual file number",{
    expect_true(nrow(get_virtual_file_list())==0)
    expect_true(C_get_file_handle_number() == 0)
})
