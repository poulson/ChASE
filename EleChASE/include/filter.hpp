/*! \file filter.hpp
 *      \brief Header file for Chebyshev filter
 *      \details This file contains a single function, an implementation of the Chebyshev polynomial filter.
 *  */

/// \cond

#ifndef ELECHFSI_FILTER
#define ELECHFSI_FILTER

#include "El.hpp"
using namespace El;
using namespace std;

#include <assert.h>

#define SEARCH_NAN 1

/// \endcond

typedef double Real;
typedef Complex<Real> C;

/** \fn filter(UpperOrLower uplo, const DistMatrix<F>& A, DistMatrix<F>& V, DistMatrix<F>& W,
           int start, int width, const int deg, int const* const degrees,
           const int deglen, const R lambda, const R lower, const R upper)
 * \brief Chebyshev filter
 * \details This function implements the Chebyshev filter. It is a filter of vectors, based on the Chebyshev polynomials (refference?).
 * The concrete implementation here surppresses the vector components that correspond to the eigenvectors of the eigenvalues larger than lambda (input parameter).
 * The result of applying this function on a number of vectors is getting them alligned to a smaller subspace, with the purpose
 * of accelerating the convergence of the process. More explanation on the reasons for using the filter here can be found in (refference).
 *
 * \param uplo          Elemental enum for specifying "LOWER" or "UPPER", part of the hermitian matrix being used by Elemental routines.
 * \param A             Elemental DistMatrix of template type F. Contains the input hermitian matrix.
 * \param V             Elemental DistMatrix of template type F. Contains the input eigenvectors, that are to be filtered.
 *                                      Each vector is a separate column in the matrix V. They have to be sorted according to the degrees parameter.
 * \param start Integer, specifying the index of the starting vector (in the matrix of vectors V), to be filtered.
 * \param width Integer, specifying the (block width) number of vectors after 'start', that are to be filtered.
 * \param deg           Integer, specifying the maximum degree that the filter could use.
 * \param degrees       Pointer to an array of integers of length deglen. Contains the degrees for each vectors that are to be filtered.
 *                                      Any value higher than 'deg' is ignored, and 'deg' is used instead. The array should be sorted ascending positive.
 * \param deglen        Integer. Length of the array of degrees.
 * \param lambda        Real. Upper estimate of the eigenvalue problem. Any vector components corresponding to eigenvalues larger
 *                                      than lambda are dampered.
 * \param lower Real. Lower bound of the eigenvalue spectrum.
 * \param upper Real. Upper bound of the eigenvalue spectrum
 *
 * \param W             Elemental DistMatrix of template type F. Contains the filtered eigenvectors after the function terminates.
 *
 * \return  int A positive integer, the number of filtered vectors
 */
template<typename F> int
filter(UpperOrLower         uplo,
       DistMatrix<F>& A,
       DistMatrix<F>&       V,
       DistMatrix<F>&       W,
       int                  start,
       int                  width,
       const int            deg,
       int const* const     degrees,
       const int            deglen,
       const Real           lambda,
       const Real           lower,
       const Real           upper)
{
  DistMatrix<F> V_view(A.Grid()), W_view(A.Grid());
  Real c = (upper + lower)/2;
  Real e = (upper - lower)/2;

  Real sigma_scale = e/(lambda - c);
  Real sigma = sigma_scale;
  Real sigma_new;
  F alpha;
  F beta;

  int fail;
  int total_vcts_filtered = 0;
  const int N = A.Height();

  int degmax = deg;
  if (degrees != NULL)
    degmax = (deg >= degrees[deglen-1]) ? deg : degrees[deglen-1];

  int local_vcts_filtered = 0, j = 0;

  if (degmax == 0) return total_vcts_filtered;

  // A = A - cI
  auto T = GetDiagonal(A);
  ShiftDiagonal(A, -c);

  alpha = F(sigma_scale/e);
  beta  = F(0.0);

  V.AssertValidSubmatrix(0, 0, N, start+width);
  W.AssertValidSubmatrix(0, 0, N, start+width);

  View(V_view, V, 0, start, N, width);
  View(W_view, W, 0, start, N, width);

#ifdef SEARCH_NAN
  //////////////////////////////////////////////////////////
  //             Here NaNs are never found
  //////////////////////////////////////////////////////////
  fail = 0;
  if( std::isnan( EntrywiseNorm( V, 1 )) ) {
    if(mpi::Rank(mpi::COMM_WORLD) == 0)
      std::cout << "V contains nan " << __LINE__ << std::endl;
    fail = 1;
  }
  if( std::isnan( EntrywiseNorm( W, 1 )) ) {
    if(mpi::Rank(mpi::COMM_WORLD) == 0)
      std::cout << "W contains nan " << __LINE__ << std::endl;
    fail = 1;
  }
  if( std::isnan( EntrywiseNorm( A, 1 )) ) {
    if(mpi::Rank(mpi::COMM_WORLD) == 0)
      std::cout << "A contains nan " << __LINE__ << std::endl;
    fail = 1;
  }
  if( fail == 1 ) {
    exit(1);
  }
#endif

  /*** First filtering step. ***/
  Hemm(LEFT, uplo, alpha, A, V_view, beta, W_view);
  //Gemm(NORMAL, NORMAL, alpha, A, V_view, beta, W_view);

#ifdef SEARCH_NAN
  //////////////////////////////////////////////////////////
  //                Here NaNs ARE found
  //////////////////////////////////////////////////////////
  fail = 0;
  if( std::isnan( EntrywiseNorm( V, 1 )) ) {
    if(mpi::Rank(mpi::COMM_WORLD) == 0)
      std::cout << "V contains nan " << __LINE__ << std::endl;
    fail = 1;
  }
  if( std::isnan( EntrywiseNorm( W, 1 )) ) {
    if(mpi::Rank(mpi::COMM_WORLD) == 0)
      std::cout << "W contains nan " << __LINE__ << std::endl;
    fail = 1;
  }
  if( std::isnan( EntrywiseNorm( A, 1 )) ) {
    if(mpi::Rank(mpi::COMM_WORLD) == 0)
      std::cout << "A contains nan " << __LINE__ << std::endl;
    fail = 1;
  }
  if( fail == 1 ) {
    if(mpi::Rank(mpi::COMM_WORLD) == 0)
      std::cout << "alpha: " << alpha << "\tstart: " << start << "\twidth: " << width << std::endl;
    exit(1);
  }
#endif

  total_vcts_filtered += width;

  // Those that had deglen[j] = 1 are now filtered.
  if (degrees != NULL)
    {
      while (j < deglen && degrees[j] == 1) ++j;
      local_vcts_filtered = j;
      width -= j;
      start += j;
    }

  /*** Remaining filtering steps. ***/
  for (int i = 2 ; i <= degmax ; ++i)
    {
      sigma_new = 1.0/(2.0/sigma_scale - sigma);

      // x = alpha(A-cI)y + beta*x
      alpha = 2.0*sigma_new / e;
      beta  = -sigma*sigma_new;

      View(V_view, V, 0, start, N, width);
      View(W_view, W, 0, start, N, width);

      // Apply translated matrix (double buffering for performance).
      if (i%2 == 0)
        {
          Hemm(LEFT, uplo, alpha, A, W_view, beta, V_view);
          //Gemm(NORMAL, NORMAL, alpha, A, W_view, beta, V_view);
        }
      else
        {
          Hemm(LEFT, uplo, alpha, A, V_view, beta, W_view);
          //Gemm(NORMAL, NORMAL, alpha, A, W_view, beta, V_view);
        }

      total_vcts_filtered += width;

      sigma = sigma_new;

      // Those that had degrees[j] = i are now filtered.
      if (degrees != NULL)
        {
          local_vcts_filtered = j;
          while (j < deglen && degrees[j] == i) ++j;
          local_vcts_filtered = j - local_vcts_filtered;
          width -= local_vcts_filtered;

          if (i%2 == 0 && local_vcts_filtered > 0)
            {
              View(V_view, V, 0, start, N, local_vcts_filtered);
              View(W_view, W, 0, start, N, local_vcts_filtered);
              W_view = V_view;
            }

          start += local_vcts_filtered;
        }

    }  // for (int i = 2 ; i <= degmax ; ++i)

  // If the filltered vectors are not in W (output), copy them there.
  if (degmax%2 == 0)
    {
      View(V_view, V, 0, start, N, width);
      View(W_view, W, 0, start, N, width);
      W_view = V_view;
    }

  // A = A + cI
  //ShiftDiagonal(A, c);
  SetDiagonal(A, T);

  return total_vcts_filtered;
}

#endif  // ELECHFSI_FILTER