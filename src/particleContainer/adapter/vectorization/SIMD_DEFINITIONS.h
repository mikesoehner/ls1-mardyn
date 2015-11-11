/***********************************************************************************//**
 *
 * \file SIMD_DEFINITIONS.h
 *
 * \brief Contains macro definitions for the intrinsics used for the vectorization
 *
 * \author Steffen Seckler
 *
 **************************************************************************************/
#pragma once

#ifndef  SIMD_DEFINITIONS_H
#define  SIMD_DEFINITIONS_H

#ifdef IN_IDE_PARSER //just for the ide parser include the simd_types.h -- normally this is not done.
    #include "./SIMD_TYPES.h"
#endif

/*
 * Check whether the file SIMD_TYPES.hpp has been included.
 * This file (SIMD_DEFINITIONS.hpp) needs some macros to be set properly by that file (SIMD_TYPES.hpp).
 */
#ifndef  SIMD_TYPES_H
    #error "SIMD_DEFINITIONS included without SIMD_TYPES! Never include this file directly! Include it only via SIMD_TYPES!"
#endif /* defined SIMD_TYPES_H */


#if VCP_VEC_TYPE==VCP_VEC_SSE3
	static inline vcp_double_vec vcp_simd_zerov() { return _mm_setzero_pd(); }

	static inline vcp_double_vec vcp_simd_lt(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm_cmplt_pd(a, b);}
	static inline vcp_double_vec vcp_simd_eq(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm_cmpeq_pd(a, b);}
	static inline vcp_double_vec vcp_simd_neq(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm_cmpneq_pd(a, b);}
	static inline vcp_double_vec vcp_simd_and(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm_and_pd(a, b);}
	static inline vcp_double_vec vcp_simd_or(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm_or_pd(a, b);}
	static inline vcp_double_vec vcp_simd_xor(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm_xor_pd(a, b);}

	static inline vcp_double_vec vcp_simd_add(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm_add_pd(a,b);}
	static inline vcp_double_vec vcp_simd_sub(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm_sub_pd(a,b);}
	static inline vcp_double_vec vcp_simd_mul(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm_mul_pd(a,b);}
	static inline vcp_double_vec vcp_simd_div(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm_div_pd(a,b);}
	static inline vcp_double_vec vcp_simd_sqrt(const vcp_double_vec& a) {return _mm_sqrt_pd(a);}

	static inline vcp_double_vec vcp_simd_set1(const double& a) {return _mm_set1_pd(a);}

	static inline vcp_double_vec vcp_simd_load(const double* const a) {return _mm_load_pd(a);}
	static inline vcp_double_vec vcp_simd_broadcast(const double* const a) {return _mm_loaddup_pd(a);}
	static inline void vcp_simd_store(double* location, vcp_double_vec a) {return _mm_store_pd(location, a);}

#elif VCP_VEC_TYPE==VCP_VEC_AVX
	static inline vcp_double_vec vcp_simd_zerov() { return _mm256_setzero_pd(); }
	static inline vcp_double_vec vcp_simd_ones() { return _mm256_castsi256_pd( _mm256_set_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF) ); }
	static inline vcp_double_vec vcp_simd_lt(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm256_cmp_pd(a, b, _CMP_LT_OS);}
	static inline vcp_double_vec vcp_simd_eq(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm256_cmp_pd(a, b, _CMP_EQ_OS);}
	static inline vcp_double_vec vcp_simd_neq(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm256_cmp_pd(a, b, _CMP_NEQ_OS);}
	static inline vcp_double_vec vcp_simd_and(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm256_and_pd(a, b);}
	static inline vcp_double_vec vcp_simd_or(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm256_or_pd(a, b);}
	static inline vcp_double_vec vcp_simd_xor(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm256_xor_pd(a, b);}

	static inline vcp_double_vec vcp_simd_add(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm256_add_pd(a,b);}
	static inline vcp_double_vec vcp_simd_sub(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm256_sub_pd(a,b);}
	static inline vcp_double_vec vcp_simd_mul(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm256_mul_pd(a,b);}
	static inline vcp_double_vec vcp_simd_div(const vcp_double_vec& a, const vcp_double_vec& b) {return _mm256_div_pd(a,b);}
	static inline vcp_double_vec vcp_simd_sqrt(const vcp_double_vec& a) {return _mm256_sqrt_pd(a);}

	static inline vcp_double_vec vcp_simd_set1(const double& a) {return _mm256_set1_pd(a);}

	static inline vcp_double_vec vcp_simd_load(const double* const a) {return _mm256_load_pd(a);}
	static inline vcp_double_vec vcp_simd_broadcast(const double* const a) {return _mm256_broadcast_sd(a);}
	static inline void vcp_simd_store(double* location, vcp_double_vec a) {return _mm256_store_pd(location, a);}

#endif























#endif /* SIMD_DEFINITIONS_H */
