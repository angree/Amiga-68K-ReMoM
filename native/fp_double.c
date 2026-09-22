/* Self-contained double-precision maths for the soft-float build.
 *
 * WHY THIS FILE EXISTS
 * Players report the non-FPU build freezing in the geoscape the moment the
 * pointer leaves the globe - on 020, 030 and 060 machines that HAVE an FPU.
 * The FPU build is healthy everywhere, and so is every machine of ours.
 *
 * The reason the FPU build is immune is the whole story: with -msoft-float gcc
 * cannot emit FPU instructions, so every sin(), asin() and atan2() becomes a
 * call into the machine's own mathieee*.library. On a machine with an FPU those
 * libraries are the FPU-using versions, and what they do there is not ours to
 * control - it depends on which Workbench, which SetPatch and which 68040/68060
 * library that particular Amiga has installed. Globe::cartToPolar calls asin()
 * with an out-of-domain argument every time the pointer leaves the sphere,
 * which is exactly the moment people see the machine stop.
 *
 * Rather than chase whose library misbehaves, the port stops asking. These
 * definitions are linked AHEAD of -lm, so the linker takes ours and the
 * OS maths libraries are never opened for double arithmetic at all. It is the
 * same move fp_single.c already makes for float multiply and divide, and for
 * the same reason: a port cannot rely on the state of someone else's LIBS:.
 *
 * Only the basic arithmetic is left to the compiler (__adddf3 and friends in
 * libgcc), which is ours and identical on every machine.
 *
 * Accuracy is around 1e-15 relative, which is far beyond anything the game
 * needs: the globe works in degrees on a 320x200 screen.
 */

/* Word view of a double. 'unsigned int' on purpose - it is 32 bits both on the
 * m68k and on the host this file is verified against, where 'unsigned long'
 * would be 64 and silently break the union. The word order follows the
 * compiler's own endianness macro so the same source can be checked against a
 * known-good libm before it ever reaches the Amiga. */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
typedef union {
	double d;
	struct { unsigned int lo, hi; } w;
} fpd_bits;
#else
typedef union {
	double d;
	struct { unsigned int hi, lo; } w;
} fpd_bits;
#endif

#define FPD_PI      3.14159265358979323846
#define FPD_PI_2    1.57079632679489661923
#define FPD_PI_4    0.78539816339744830962
#define FPD_LN2     0.69314718055994530942
#define FPD_LOG2E   1.44269504088896340736

static double fpd_nan(void)
{
	fpd_bits u;
	u.w.hi = 0x7FF80000u;
	u.w.lo = 0;
	return u.d;
}

static double fpd_inf(int negative)
{
	fpd_bits u;
	u.w.hi = negative ? 0xFFF00000u : 0x7FF00000u;
	u.w.lo = 0;
	return u.d;
}

static int fpd_isnan(double x)
{
	fpd_bits u;
	u.d = x;
	return ((u.w.hi & 0x7FF00000u) == 0x7FF00000u) &&
	       (((u.w.hi & 0x000FFFFFu) | u.w.lo) != 0);
}

static int fpd_isinf(double x)
{
	fpd_bits u;
	u.d = x;
	return ((u.w.hi & 0x7FFFFFFFu) == 0x7FF00000u) && (u.w.lo == 0);
}

double fabs(double x)
{
	fpd_bits u;
	u.d = x;
	u.w.hi &= 0x7FFFFFFFu;
	return u.d;
}

/* Split x into a mantissa in [0.5,1) and a power of two. Subnormals are scaled
 * up first; the game never produces them, but a maths routine that mishandles
 * them is a trap for later. */
double frexp(double x, int *e)
{
	fpd_bits u;
	int ex;
	u.d = x;
	ex = (int)((u.w.hi >> 20) & 0x7FF);
	*e = 0;
	if (ex == 0x7FF) return x;                 /* inf or nan */
	if (ex == 0) {
		if ((u.w.hi & 0x000FFFFFu) == 0 && u.w.lo == 0) return x;   /* zero */
		u.d = x * 18014398509481984.0;         /* 2^54 */
		ex = (int)((u.w.hi >> 20) & 0x7FF);
		*e = -54;
	}
	*e += ex - 1022;
	u.w.hi = (u.w.hi & 0x800FFFFFu) | 0x3FE00000u;
	return u.d;
}

double ldexp(double x, int e)
{
	fpd_bits u;
	int ex;
	if (x == 0.0 || fpd_isnan(x) || fpd_isinf(x)) return x;
	u.d = x;
	ex = (int)((u.w.hi >> 20) & 0x7FF);
	if (ex == 0) {                              /* subnormal: normalise first */
		u.d = x * 18014398509481984.0;
		ex = (int)((u.w.hi >> 20) & 0x7FF);
		e -= 54;
	}
	ex += e;
	if (ex >= 0x7FF) return fpd_inf(x < 0.0);
	if (ex <= 0) {
		if (ex < -54) return x < 0.0 ? -0.0 : 0.0;
		u.w.hi = (u.w.hi & 0x800FFFFFu) | ((unsigned int)(ex + 54) << 20);
		return u.d * 5.5511151231257827e-17;    /* 2^-54 */
	}
	u.w.hi = (u.w.hi & 0x800FFFFFu) | ((unsigned int)ex << 20);
	return u.d;
}

double floor(double x)
{
	double t;
	if (fpd_isnan(x) || fpd_isinf(x)) return x;
	if (fabs(x) >= 4503599627370496.0) return x;   /* 2^52: already integral */
	t = (double)(long)x;
	if (x < 0.0 && t != x) t -= 1.0;
	/* values beyond long need the slow path, but the game never gets there */
	return t;
}

double ceil(double x)
{
	double t;
	if (fpd_isnan(x) || fpd_isinf(x)) return x;
	if (fabs(x) >= 4503599627370496.0) return x;
	t = (double)(long)x;
	if (x > 0.0 && t != x) t += 1.0;
	return t;
}

double sqrt(double x)
{
	double m, y;
	int e;
	if (fpd_isnan(x)) return x;
	if (x < 0.0) return fpd_nan();
	if (x == 0.0 || fpd_isinf(x)) return x;

	m = frexp(x, &e);                 /* x = m * 2^e, m in [0.5,1) */
	if (e & 1) { m *= 0.5; e++; }     /* make the exponent even */

	/* linear seed, then Newton: each step doubles the correct digits, so four
	 * take a 3-digit guess past the 53 bits a double holds */
	y = 0.41731 + 0.59016 * m;
	y = 0.5 * (y + m / y);
	y = 0.5 * (y + m / y);
	y = 0.5 * (y + m / y);
	y = 0.5 * (y + m / y);
	return ldexp(y, e >> 1);
}

/* --- sin/cos ------------------------------------------------------------
 * Reduce to |r| <= pi/4 by subtracting multiples of pi/2, taking pi/2 in two
 * pieces so the subtraction stays exact for the angles a game produces, then
 * evaluate the usual minimax polynomials. */

#define FPD_PIO2_HI 1.57079632673412561417
#define FPD_PIO2_LO 6.07710050650619224932e-11

static double fpd_sin_poly(double x)
{
	double z = x * x;
	return x + x * z * (-1.66666666666666324348e-01 +
	           z * ( 8.33333333332248946124e-03 +
	           z * (-1.98412698298579493134e-04 +
	           z * ( 2.75573137070700676789e-06 +
	           z * (-2.50507602534068634195e-08 +
	           z *   1.58969099521155010221e-10)))));
}

static double fpd_cos_poly(double x)
{
	double z = x * x;
	return 1.0 + z * (-5.00000000000000000000e-01 +
	            z * ( 4.16666666666666019037e-02 +
	            z * (-1.38888888888741095749e-03 +
	            z * ( 2.48015872894767294178e-05 +
	            z * (-2.75573143513906633035e-07 +
	            z * ( 2.08757232129817482790e-09 +
	            z *  -1.13596475577881948265e-11))))));
}

static int fpd_reduce(double x, double *r)
{
	double n, y;
	long q;
	if (fabs(x) < FPD_PI_4) { *r = x; return 0; }
	n = x * (2.0 / FPD_PI);
	q = (long)(n < 0.0 ? n - 0.5 : n + 0.5);
	y = (double)q;
	*r = (x - y * FPD_PIO2_HI) - y * FPD_PIO2_LO;
	return (int)(q & 3);
}

double sin(double x)
{
	double r;
	int q;
	if (fpd_isnan(x) || fpd_isinf(x)) return fpd_nan();
	q = fpd_reduce(x, &r);
	switch (q) {
	case 0:  return fpd_sin_poly(r);
	case 1:  return fpd_cos_poly(r);
	case 2:  return -fpd_sin_poly(r);
	default: return -fpd_cos_poly(r);
	}
}

double cos(double x)
{
	double r;
	int q;
	if (fpd_isnan(x) || fpd_isinf(x)) return fpd_nan();
	q = fpd_reduce(x, &r);
	switch (q) {
	case 0:  return fpd_cos_poly(r);
	case 1:  return -fpd_sin_poly(r);
	case 2:  return -fpd_cos_poly(r);
	default: return fpd_sin_poly(r);
	}
}

double tan(double x)
{
	double c = cos(x);
	if (c == 0.0) return fpd_inf(sin(x) < 0.0);
	return sin(x) / c;
}

/* --- atan / atan2 -------------------------------------------------------
 * Fold the argument into [0, tan(pi/8)] using the two standard identities,
 * then one polynomial covers the rest. */

static double fpd_atan_poly(double x)
{
	double z = x * x;
	return x + x * z * (-3.33333333333331e-01 +
	           z * ( 1.99999999996591e-01 +
	           z * (-1.42857141019101e-01 +
	           z * ( 1.11111100292184e-01 +
	           z * (-9.09088713343240e-02 +
	           z * ( 7.69187620504482e-02 +
	           z * (-6.66107313738753e-02 +
	           z * ( 5.83357013379057e-02 +
	           z * (-4.97687799461593e-02 +
	           z * ( 3.66185654866172e-02 +
	           z * (-1.62858201153657e-02 +
	           z *   3.28560996002932e-03)))))))))));
}

double atan(double x)
{
	int neg = 0;
	double y;
	if (fpd_isnan(x)) return x;
	if (x < 0.0) { x = -x; neg = 1; }
	if (fpd_isinf(x)) { y = FPD_PI_2; }
	else if (x > 2.414213562373095) {            /* tan(3pi/8) */
		y = FPD_PI_2 - fpd_atan_poly(1.0 / x);
	} else if (x > 0.4142135623730950) {         /* tan(pi/8) */
		y = FPD_PI_4 + fpd_atan_poly((x - 1.0) / (x + 1.0));
	} else {
		y = fpd_atan_poly(x);
	}
	return neg ? -y : y;
}

double atan2(double y, double x)
{
	if (fpd_isnan(x) || fpd_isnan(y)) return fpd_nan();
	if (x == 0.0) {
		if (y > 0.0) return FPD_PI_2;
		if (y < 0.0) return -FPD_PI_2;
		return 0.0;                              /* atan2(0,0): 0, like libm */
	}
	if (x > 0.0) return atan(y / x);
	if (y >= 0.0) return atan(y / x) + FPD_PI;
	return atan(y / x) - FPD_PI;
}

/* --- asin / acos --------------------------------------------------------
 * The argument is CLAMPED. Off the globe cartToPolar hands asin() a ratio
 * greater than one, and the honest answer there is the horizon, not a NaN
 * that then poisons every calculation downstream. */

double asin(double x)
{
	if (fpd_isnan(x)) return x;
	if (x >= 1.0)  return FPD_PI_2;
	if (x <= -1.0) return -FPD_PI_2;
	return atan2(x, sqrt((1.0 - x) * (1.0 + x)));
}

double acos(double x)
{
	if (fpd_isnan(x)) return x;
	if (x >= 1.0)  return 0.0;
	if (x <= -1.0) return FPD_PI;
	return FPD_PI_2 - asin(x);
}

/* --- log / exp / pow ---------------------------------------------------- */

double log(double x)
{
	double m, z, w, r;
	int e;
	if (fpd_isnan(x)) return x;
	if (x < 0.0) return fpd_nan();
	if (x == 0.0) return fpd_inf(1);
	if (fpd_isinf(x)) return x;

	m = frexp(x, &e);
	if (m < 0.70710678118654752440) { m *= 2.0; e--; }   /* centre on 1 */

	z = (m - 1.0) / (m + 1.0);
	w = z * z;
	r = z * (2.0 +
	    w * (0.666666666666735130 +
	    w * (0.399999999940941908 +
	    w * (0.285714287436623914 +
	    w * (0.222221984321497839 +
	    w * (0.181835721616180501 +
	    w * (0.153138376992093733 +
	    w *  0.147981986051165859)))))));
	return r + (double)e * FPD_LN2;
}

double log10(double x)
{
	return log(x) * 0.43429448190325182765;
}

double exp(double x)
{
	double n, r, y;
	long k;
	if (fpd_isnan(x)) return x;
	if (x > 709.78) return fpd_inf(0);
	if (x < -745.13) return 0.0;

	n = x * FPD_LOG2E;
	k = (long)(n < 0.0 ? n - 0.5 : n + 0.5);
	/* ln2 in two pieces keeps the reduced argument accurate */
	r = (x - (double)k * 6.93147180369123816490e-01)
	       - (double)k * 1.90821492927058770002e-10;

	y = 1.0 + r * (1.0 +
	        r * (0.5 +
	        r * (1.66666666666666019037e-01 +
	        r * (4.16666666666666019037e-02 +
	        r * (8.33333333333332176360e-03 +
	        r * (1.38888888888888880e-03 +
	        r * (1.98412698412698413e-04 +
	        r *  2.48015873015873016e-05)))))));
	return ldexp(y, (int)k);
}

double pow(double x, double y)
{
	long iy;
	if (fpd_isnan(x) || fpd_isnan(y)) return fpd_nan();
	if (y == 0.0) return 1.0;
	if (x == 0.0) return (y > 0.0) ? 0.0 : fpd_inf(0);
	if (y == 1.0) return x;
	if (y == 2.0) return x * x;

	/* A negative base is only defined for whole exponents, and the game does
	 * use small whole powers - so handle those exactly instead of going
	 * through log(), which would hand back a NaN. */
	iy = (long)y;
	if ((double)iy == y && iy > -64 && iy < 64) {
		double r = 1.0, b = x;
		long n = iy < 0 ? -iy : iy;
		while (n) {
			if (n & 1) r *= b;
			b *= b;
			n >>= 1;
		}
		return (iy < 0) ? 1.0 / r : r;
	}
	if (x < 0.0) return fpd_nan();
	return exp(y * log(x));
}

double fmod(double x, double y)
{
	double q;
	if (fpd_isnan(x) || fpd_isnan(y) || y == 0.0 || fpd_isinf(x)) return fpd_nan();
	if (fpd_isinf(y)) return x;
	q = x / y;
	q = (q < 0.0) ? ceil(q) : floor(q);
	return x - q * y;
}

/* Float wrappers: the game is C++ and overloads resolve to these. */
float sqrtf(float x)            { return (float)sqrt((double)x); }
float sinf(float x)             { return (float)sin((double)x); }
float cosf(float x)             { return (float)cos((double)x); }
float atan2f(float y, float x)  { return (float)atan2((double)y, (double)x); }
float floorf(float x)           { return (float)floor((double)x); }
float ceilf(float x)            { return (float)ceil((double)x); }
float fabsf(float x)            { return (float)fabs((double)x); }
