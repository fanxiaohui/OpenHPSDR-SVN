sources = after-ccopy-from.c /
after-ccopy-to.c /
after-hccopy-from.c /
after-hccopy-to.c /
after-rcopy-from.c /
after-rcopy-to.c /
allocate.c /
aset.c /
bench-cost-postprocess.c /
bench-exit.c /
bench-main.c /
can-do.c /
caset.c /
dotens2.c /
info.c /
main.c /
mflops.c /
mp.c /
my-getopt.c /
ovtpvt.c /
pow2.c /
problem.c /
report.c /
speed.c /
tensor.c /
timer.c /
useropt.c /
util.c /
verify.c /
verify-dft.c /
verify-lib.c /
verify-r2r.c /
verify-rdft2.c /
zero.c

LOCAL_SRC_FILES += $(sources:%=fftw3/libbench2.%)
