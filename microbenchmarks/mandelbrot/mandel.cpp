#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <time.h>

//#include <getopt.h>
#include "mandel.h"
#include "UMEBitmap.h"

#include "mandel_ume.h"

// Introducing inline assembly forces compiler to generate
#define BREAK_COMPILER_OPTIMIZATION() __asm__ ("NOP");

// define RDTSC getter function
#if defined(__i386__)
static __inline__ unsigned long long __rdtsc(void)
{
    unsigned long long int x;
    __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
    return x;
}
#elif defined(__x86_64__)
static __inline__ unsigned long long __rdtsc(void)
{
    unsigned hi, lo;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}
#endif

typedef unsigned long long TIMING_RES;

void mandel_basic(unsigned char *image, const struct spec *s);
void mandel_avx(unsigned char *image, const struct spec *s);
void mandel_sse2(unsigned char *image, const struct spec *s);

void mandel_basic(unsigned char *image, const struct spec *s)
{
    float xdiff = s->xlim[1] - s->xlim[0];
    float ydiff = s->ylim[1] - s->ylim[0];
    float iter_scale = 1.0f / s->iterations;
    float depth_scale = float(s->depth - 1);
   // #pragma omp parallel for schedule(dynamic, 1)
    for (int y = 0; y < s->height; y++) {
        for (int x = 0; x < s->width; x++) {
            float cr = x * xdiff / s->width  + s->xlim[0];
            float ci = y * ydiff / s->height + s->ylim[0];
            float zr = cr;
            float zi = ci;
            int k = 0;
            float mk = 0.0f;
            while (++k < s->iterations) {
                float zr1 = zr * zr - zi * zi + cr;
                float zi1 = zr * zi + zr * zi + ci;
                zr = zr1;
                zi = zi1;
                mk += 1.0f;
                if (zr * zr + zi * zi >= 4.0f)
                    break;
            }
            mk *= iter_scale;
            mk = sqrtf(mk);
            mk *= depth_scale;
            int pixel = int(mk);
            image[y * s->width * 3 + x * 3 + 0] = pixel;
            image[y * s->width * 3 + x * 3 + 1] = pixel;
            image[y * s->width * 3 + x * 3 + 2] = pixel;
        }
    }
}

#ifdef __x86_64__
#include <cpuid.h>

static inline int
is_avx_supported(void)
{
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    __get_cpuid(1, &eax, &ebx, &ecx, &edx);
    return ecx & bit_AVX ? 1 : 0;
}
#endif // __x86_64__

int
main(int argc, char *argv[])
{
    int ITERATIONS = 10;

    /* Config */
    struct spec spec;
    spec.width = 1024;
    spec.height = 1024;
    spec.depth = 256;
    spec.xlim[0] = -2.5;
    spec.xlim[1] = 1.5;
    spec.ylim[0] = -1.5;
    spec.ylim[1] = 1.5;
    spec.iterations = 256;


    TIMING_RES  t_scalar_f, 
                t_SSE2_f, 
                t_AVX_f,
                t_UME_SIMD1_32f,
                t_UME_SIMD2_32f,
                t_UME_SIMD4_32f,
                t_UME_SIMD8_32f,
                t_UME_SIMD16_32f,
                t_UME_SIMD32_32f;

    float   t_scalar_f_avg = 0.0f,
            t_SSE2_f_avg = 0.0f,
            t_AVX_f_avg = 0.0f,
            t_UME_SIMD1_32f_avg = 0.0f,
            t_UME_SIMD2_32f_avg = 0.0f,
            t_UME_SIMD4_32f_avg = 0.0f,
            t_UME_SIMD8_32f_avg = 0.0f,
            t_UME_SIMD16_32f_avg = 0.0f,
            t_UME_SIMD32_32f_avg = 0.0f;

    UME::Bitmap bmp(spec.width, spec.height, UME::PIXEL_TYPE_RGB);
    uint8_t* image = bmp.GetRasterData();

    std::cout << "The result is amount of time it takes to calculate mandelbrot algorithm.\n"
        "All timing results in clock cycles. \n"
        "Speedup calculated with scalar floating point result as reference.\n\n"
        "SIMD version uses following operations: \n"
        "   32f vectors: FULL-CONSTR, LOAD-CONSTR, ADDS (operator+ RHS scalar), \n"
        "              FMULADDV, MULV (operator*), SUBV (operator-), ADDV (operator+)\n"
        "              ASSIGNV (operator=), CMPLTV (operator<), MADDV, SQRT, ROUND\n"
        "              FTOI\n"
        "   32i vectors: STORE\n"
        "   masks:       HLOR\n"
        " Algorithm parameters are:"
        "     image width: " << spec.width << "\n"
        "     image height: " << spec.height << "\n"
        "     iteration depth: " << spec.depth << "\n"
        "     # of iterations: " << spec.iterations << "\n\n";

    for (int i = 0; i < ITERATIONS; i++) {
        TIMING_RES start, end;

        start = __rdtsc();
        mandel_basic(image, &spec);
        end = __rdtsc();
        t_scalar_f = end - start;
        t_scalar_f_avg = 1.0f / (1.0f + float(i))  * (float(t_scalar_f) - t_scalar_f_avg);

        // Saving to file to make sure the results generated are correct
        bmp.SaveToFile("mandel_basic.bmp");
        bmp.ClearTarget(0, 255, 0);
    }

    std::cout << "Scalar code (float): " << (long)t_scalar_f_avg
                                         << " (speedup: 1.0x)"
                                         << std::endl;

#if defined __SSE2__ | _M_IX86_FP == 2 
    for (int i = 0; i < ITERATIONS; i++) {
        TIMING_RES start, end;

        start = __rdtsc();
        mandel_sse2(image, &spec);
        end = __rdtsc();
        t_SSE2_f = end - start;
        t_SSE2_f_avg = 1.0f / (1.0f + float(i))  * (float(t_SSE2_f) - t_SSE2_f_avg);

        // Saving to file to make sure the results generated are correct
        bmp.SaveToFile("mandel_sse2.bmp");
        bmp.ClearTarget(0, 255, 0);
    }

    std::cout << "SSE2 intrinsic code (float): " << (long)t_SSE2_f_avg
                                       << " (speedup: "
                                       << float(t_scalar_f_avg) / float(t_SSE2_f_avg) << ")\n";
#endif

#if defined __AVX__
    for (int i = 0; i < ITERATIONS; i++) {
        TIMING_RES start, end;

        start = __rdtsc();
        mandel_avx(image, &spec);
        end = __rdtsc();
        t_AVX_f = end - start;
        t_AVX_f_avg = 1.0f / (1.0f + float(i))  * (float(t_AVX_f) - t_AVX_f_avg);

        // Saving to file to make sure the results generated are correct
        bmp.SaveToFile("mandel_avx.bmp");
        bmp.ClearTarget(0, 255, 0);
    }

    std::cout << "AVX intrinsic code (float): " << (long)t_AVX_f_avg
        << " (speedup: "
        << float(t_scalar_f_avg) / float(t_AVX_f_avg) << ")\n";
#endif

    // Test UME::SIMD::SIMD1_32f
    for (int i = 0; i < ITERATIONS; i++) {
        TIMING_RES start, end;

        start = __rdtsc();
        mandel_umesimd<UME::SIMD::SIMD1_32f>(image, &spec);
        end = __rdtsc();
        t_UME_SIMD1_32f = end - start;
        t_UME_SIMD1_32f_avg = 1.0f / (1.0f + float(i))  * (float(t_UME_SIMD1_32f) - t_UME_SIMD1_32f_avg);

        // Saving to file to make sure the results generated are correct
        bmp.SaveToFile("mandel_umesimd_1_32f.bmp");
        bmp.ClearTarget(0, 255, 0);
    }

    std::cout << "SIMD code (1x32f): " << (long)t_UME_SIMD1_32f_avg
        << " (speedup: "
        << float(t_scalar_f_avg) / float(t_UME_SIMD1_32f_avg) << ")"
        << std::endl;

    // Test UME::SIMD::SIMD2_32f
    for (int i = 0; i < ITERATIONS; i++) {
        TIMING_RES start, end;

        start = __rdtsc();
        mandel_umesimd<UME::SIMD::SIMD2_32f>(image, &spec);
        end = __rdtsc();
        t_UME_SIMD2_32f = end - start;
        t_UME_SIMD2_32f_avg = 1.0f / (1.0f + float(i))  * (float(t_UME_SIMD2_32f) - t_UME_SIMD2_32f_avg);

        // Saving to file to make sure the results generated are correct
        bmp.SaveToFile("mandel_umesimd_2_32f.bmp");
        bmp.ClearTarget(0, 255, 0);
    }

    std::cout << "SIMD code (2x32f): " << (long)t_UME_SIMD2_32f_avg
        << " (speedup: "
        << float(t_scalar_f_avg) / float(t_UME_SIMD2_32f_avg) << ")"
        << std::endl;

    // Test UME::SIMD::SIMD4_32f
    for (int i = 0; i < ITERATIONS; i++) {
        TIMING_RES start, end;

        start = __rdtsc();
        mandel_umesimd<UME::SIMD::SIMD4_32f>(image, &spec);
        end = __rdtsc();
        t_UME_SIMD4_32f = end - start;
        t_UME_SIMD4_32f_avg = 1.0f / (1.0f + float(i))  * (float(t_UME_SIMD4_32f) - t_UME_SIMD4_32f_avg);

        // Saving to file to make sure the results generated are correct
        bmp.SaveToFile("mandel_umesimd_4_32f.bmp");
        bmp.ClearTarget(0, 255, 0);
    }

    std::cout << "SIMD code (4x32f): " << (long)t_UME_SIMD4_32f_avg
        << " (speedup: "
        << float(t_scalar_f_avg) / float(t_UME_SIMD4_32f_avg) << ")"
        << std::endl;

    // Test UME::SIMD::SIMD8_32f
    for (int i = 0; i < ITERATIONS; i++) {
        TIMING_RES start, end;

        start = __rdtsc();
        mandel_umesimd<UME::SIMD::SIMD8_32f>(image, &spec);
        end = __rdtsc();
        t_UME_SIMD8_32f = end - start;
        t_UME_SIMD8_32f_avg = 1.0f / (1.0f + float(i))  * (float(t_UME_SIMD8_32f) - t_UME_SIMD8_32f_avg);

        // Saving to file to make sure the results generated are correct
        bmp.SaveToFile("mandel_umesimd_8_32f.bmp");
        bmp.ClearTarget(0, 255, 0);
    }

    std::cout << "SIMD code (8x32f): " << (long)t_UME_SIMD8_32f_avg
        << " (speedup: "
        << float(t_scalar_f_avg) / float(t_UME_SIMD8_32f_avg) << ")"
        << std::endl;

    // Test UME::SIMD::SIMD16_32f
    for (int i = 0; i < ITERATIONS; i++) {
        TIMING_RES start, end;

        start = __rdtsc();
        mandel_umesimd<UME::SIMD::SIMD16_32f>(image, &spec);
        end = __rdtsc();
        t_UME_SIMD16_32f = end - start;
        t_UME_SIMD16_32f_avg = 1.0f / (1.0f + float(i))  * (float(t_UME_SIMD16_32f) - t_UME_SIMD16_32f_avg);

        // Saving to file to make sure the results generated are correct
        bmp.SaveToFile("mandel_umesimd_16_32f.bmp");
        bmp.ClearTarget(0, 255, 0);
    }

    std::cout << "SIMD code (16x32f): " << (long)t_UME_SIMD16_32f_avg
        << " (speedup: "
        << float(t_scalar_f_avg) / float(t_UME_SIMD16_32f_avg) << ")"
        << std::endl;

    // Test UME::SIMD::SIMD32_32f
    for (int i = 0; i < ITERATIONS; i++) {
        TIMING_RES start, end;

        start = __rdtsc();
        mandel_umesimd<UME::SIMD::SIMD32_32f>(image, &spec);
        end = __rdtsc();
        t_UME_SIMD32_32f = end - start;
        t_UME_SIMD32_32f_avg = 1.0f / (1.0f + float(i))  * (float(t_UME_SIMD32_32f) - t_UME_SIMD32_32f_avg);

        // Saving to file to make sure the results generated are correct
        bmp.SaveToFile("mandel_umesimd_32_32f.bmp");
        bmp.ClearTarget(0, 255, 0);
    }

    std::cout << "SIMD code (32x32f): " << (long)t_UME_SIMD32_32f_avg
        << " (speedup: "
        << float(t_scalar_f_avg) / float(t_UME_SIMD32_32f_avg) << ")"
        << std::endl;

    return 0;
}