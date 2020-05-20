/******************************************************************************
 *
 * $Id$
 *
 * Project:  WindNinja
 * Purpose:  Finite Element Method operations
 * Author:   Natalie Wagenbrenner <nwagenbrenner@gmail.com>
 *
 ******************************************************************************
 *
 * THIS SOFTWARE WAS DEVELOPED AT THE ROCKY MOUNTAIN RESEARCH STATION (RMRS)
 * MISSOULA FIRE SCIENCES LABORATORY BY EMPLOYEES OF THE FEDERAL GOVERNMENT
 * IN THE COURSE OF THEIR OFFICIAL DUTIES. PURSUANT TO TITLE 17 SECTION 105
 * OF THE UNITED STATES CODE, THIS SOFTWARE IS NOT SUBJECT TO COPYRIGHT
 * PROTECTION AND IS IN THE PUBLIC DOMAIN. RMRS MISSOULA FIRE SCIENCES
 * LABORATORY ASSUMES NO RESPONSIBILITY WHATSOEVER FOR ITS USE BY OTHER
 * PARTIES,  AND MAKES NO GUARANTEES, EXPRESSED OR IMPLIED, ABOUT ITS QUALITY,
 * RELIABILITY, OR ANY OTHER CHARACTERISTIC.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************/
#include "finiteElementMethod.h"


FiniteElementMethod::FiniteElementMethod()
{
    NUMNP=0;

    //Pointers to dynamically allocated memory
    DIAG=NULL;
    PHI=NULL;
    RHS=NULL;
    SK=NULL;
    row_ptr=NULL;
    col_ind=NULL;
}

/**
 * Copy constructor.
 * @param A Copied value.
 */

FiniteElementMethod::FiniteElementMethod(FiniteElementMethod const& A )
{
    PHI=A.PHI;
    DIAG=A.DIAG;

    NUMNP=A.NUMNP;
    RHS=A.RHS;
    SK=A.SK;
    row_ptr=A.row_ptr;
    col_ind=A.col_ind;
}

/**
 * Equals operator.
 * @param A Value to set equal to.
 * @return a copy of an object
 */

FiniteElementMethod& FiniteElementMethod::operator=(FiniteElementMethod const& A)
{
    if(&A != this) {
        PHI=A.PHI;
        DIAG=A.DIAG;

        NUMNP=A.NUMNP;
        RHS=A.RHS;
        SK=A.SK;
        row_ptr=A.row_ptr;
        col_ind=A.col_ind;
    }
    return *this;
}

FiniteElementMethod::~FiniteElementMethod()      //destructor
{
    Deallocate();
}

void FiniteElementMethod::Discretize(const Mesh &mesh, WindNinjaInputs &input, wn_3dVectorField &U0) 
{

}

void FiniteElementMethod::SetBoundaryConditions(const Mesh &mesh, WindNinjaInputs &input)
{

}

//  CG solver
//    This solver is fastest, but is not monotonic convergence (residual oscillates a bit up and down)
//    If this solver diverges, try MINRES from PetSc below...
/**Method called in ninja::simulate_wind() to solve the matrix equations.
 * This is a congugate gradient solver.
 * It seems to be the fastest, but is not monotonic convergence (residual oscillates a bit up and down).
 * If this solver diverges, try the MINRES from PetSc which is commented out below...
 * @param A Stiffness matrix in Ax=b matrix equation.  Storage is symmetric compressed sparse row storage.
 * @param b Right hand side of matrix equations.
 * @param x Vector to store solution in.
 * @param row_ptr Vector used to index to a row in A.
 * @param col_ind Vector storing the column number of corresponding value in A.
 * @param NUMNP Number of nodal points, so also the size of b, x, and row_ptr.
 * @param max_iter Maximum number of iterations to do.
 * @param print_iters How often to print out solver information.
 * @param tol Convergence tolerance to stop at.
 * @return Returns true if solver converges and completes properly.
 */
bool FiniteElementMethod::Solve(WindNinjaInputs &input, int NUMNP, int max_iter, int print_iters, double tol)
{
    double *A = SK;
    double *b = RHS;
    double *x = PHI;

    //stuff for sparse BLAS MV multiplication
    char transa='n';
    double one=1.E0, zero=0.E0;
    char matdescra[6];
    matdescra[0]='s';	//symmetric
    matdescra[1]='u';	//upper triangle stored
    matdescra[2]='n';	//non-unit diagonal
    matdescra[3]='c';	//c-style array (ie 0 is index of first element, not 1 like in Fortran)

    FILE *convergence_history;
    int i, j;
    double *p, *z, *q, *r;
    double alpha, beta, rho, rho_1, normb, resid;
    double residual_percent_complete, residual_percent_complete_old, time_percent_complete, start_resid;
    residual_percent_complete = 0.0;

    residual_percent_complete_old = -1.;

    Preconditioner M;
    if(M.initialize(NUMNP, A, row_ptr, col_ind, M.SSOR, matdescra)==false)
    {
        input.Com->ninjaCom(ninjaComClass::ninjaWarning, "Initialization of SSOR preconditioner failed, trying Jacobi preconditioner...");
        if(M.initialize(NUMNP, A, row_ptr, col_ind, M.Jacobi, matdescra)==false)
            throw std::runtime_error("Initialization of Jacobi preconditioner failed.");
    }

//#define NINJA_DEBUG_VERBOSE
#ifdef NINJA_DEBUG_VERBOSE
    if((convergence_history = fopen ("convergence_history.txt", "w")) == NULL)
        throw std::runtime_error("A convergence_history file to write to cannot be created.\nIt may be in use by another program.");

    fprintf(convergence_history,"\nIteration\tResidual\tResidual_check");
#endif //NINJA_DEBUG_VERBOSE

    p=new double[NUMNP];
    z=new double[NUMNP];
    q=new double[NUMNP];
    r=new double[NUMNP];
    //Ax=new double[NUMNP];
    //Ap=new double[NUMNP];
    //Anorm=new double[NUMNP];



    //matrix vector multiplication A*x=Ax
    mkl_dcsrmv(&transa, &NUMNP, &NUMNP, &one, matdescra, A, col_ind, row_ptr, &row_ptr[1], x, &zero, r);

    for(i=0;i<NUMNP;i++){
        r[i]=b[i]-r[i];                  //calculate the initial residual
    }

    normb = cblas_dnrm2(NUMNP, b, 1);		//calculate the 2-norm of b
    //normb = nrm2(NUMNP, b);

    if (normb == 0.0)
        normb = 1.;

    //compute 2 norm of r
    resid = cblas_dnrm2(NUMNP, r, 1) / normb;
    //resid = nrm2(NUMNP, r) / normb;

    if (resid <= tol)
    {
        tol = resid;
        max_iter = 0;
        return true;
    }

    //start iterating---------------------------------------------------------------------------------------
    for (int i = 1; i <= max_iter; i++)
    {

        M.solve(r, z, row_ptr, col_ind);	//apply preconditioner

        rho = cblas_ddot(NUMNP, z, 1, r, 1);
        //rho = dot(NUMNP, z, r);

        if (i == 1)
        {
            cblas_dcopy(NUMNP, z, 1, p, 1);
        }else {
            beta = rho / rho_1;

#pragma omp parallel for
            for(j=0; j<NUMNP; j++)
                p[j] = z[j] + beta*p[j];
        }

        //matrix vector multiplication!!!		q = A*p;
        mkl_dcsrmv(&transa, &NUMNP, &NUMNP, &one, matdescra, A, col_ind, row_ptr, &row_ptr[1], p, &zero, q);

        alpha = rho / cblas_ddot(NUMNP, p, 1, q, 1);
        //alpha = rho / dot(NUMNP, p, q);

        cblas_daxpy(NUMNP, alpha, p, 1, x, 1);	//x = x + alpha * p;
        //axpy(NUMNP, alpha, p, x);

        cblas_daxpy(NUMNP, -alpha, q, 1, r, 1);	//r = r - alpha * q;
        //axpy(NUMNP, -alpha, q, r);

        resid = cblas_dnrm2(NUMNP, r, 1) / normb;	//compute resid
        //resid = nrm2(NUMNP, r) / normb;

        if(i==1)
            start_resid = resid;

        if((i%print_iters)==0)
        {

#ifdef NINJA_DEBUG_VERBOSE
            input.Com->ninjaCom(ninjaComClass::ninjaDebug, "Iteration = %d\tResidual = %lf\ttol = %lf", i, resid, tol);
            fprintf(convergence_history,"\n%ld\t%lf\t%lf",i,resid,tol);
#endif //NINJA_DEBUG_VERBOSE

            residual_percent_complete=100-100*((resid-tol)/(start_resid-tol));
            if(residual_percent_complete<residual_percent_complete_old)
                residual_percent_complete=residual_percent_complete_old;
            if(residual_percent_complete<0.)
                residual_percent_complete=0.;
            else if(residual_percent_complete>100.)
                residual_percent_complete=100.0;

            time_percent_complete=1.8*exp(0.0401*residual_percent_complete);
            if(time_percent_complete >= 99.0)
                time_percent_complete = 99.0;
            residual_percent_complete_old=residual_percent_complete;
            //fprintf(convergence_history,"\n%ld\t%lf\t%lf",i,residual_percent_complete, time_percent_complete);
#ifdef NINJAFOAM
            //If its a foam+diurnal run, progressWeight==0.80, which means we need to modify the
            //diurnal time_percent_complete
            if(input.Com->progressWeight!=1.0)
            {
                time_percent_complete = (input.Com->progressWeight*100)+(1.0-input.Com->progressWeight)*time_percent_complete;
            }
#endif //NINJAFOAM
            input.Com->ninjaCom(ninjaComClass::ninjaSolverProgress, "%d",(int) (time_percent_complete+0.5)); //Tell the GUI what the percentage to complete for the ninja is
        }

        if (resid <= tol)	//check residual against tolerance
        {
            break;
        }

        //cout<<"resid = "<<resid<<endl;

        rho_1 = rho;

    }	//end iterations--------------------------------------------------------------------------------------------

    if(p)
    {
        delete[] p;
        p=NULL;
    }
    if(z)
    {
        delete[] z;
        z=NULL;
    }
    if(q)
    {
        delete[] q;
        q=NULL;
    }
    if(r)
    {
        delete[] r;
        r=NULL;
    }

#ifdef NINJA_DEBUG_VERBOSE
    fclose(convergence_history);
#endif //NINJA_DEBUG_VERBOSE

    if(resid>tol)
    {
        throw std::runtime_error("Solution did not converge.\nMAXITS reached.");
    }else{
        time_percent_complete = 100; //When the solver finishes, set it to 100
        input.Com->ninjaCom(ninjaComClass::ninjaSolverProgress, "%d",(int) (time_percent_complete+0.5));
        return true;
    }
}

//  MINRES from PetSc (found in google code search)
//    This solver seems to be monotonic in its convergence (residual always goes down)
//    Could use this if CG diverges, but haven't seen divergence yet...
bool FiniteElementMethod::SolveMinres(WindNinjaInputs &input, int NUMNP, int max_iter, int print_iters, double tol)
{
    double *A = SK;
    double *b = RHS;
    double *x = PHI;

  int i,j, n;
  double alpha,malpha,beta,mbeta,ibeta,betaold,eta,c=1.0,ceta,cold=1.0,coold,s=0.0,sold=0.0,soold;
  double rho0,rho1,irho1,rho2,mrho2,rho3,mrho3,dp = 0.0;
  double rnorm, bnorm;
  double np;	//this is the residual
  double residual_percent_complete_old, residual_percent_complete, time_percent_complete, start_resid;
  double            *R, *Z, *U, *V, *W, *UOLD, *VOLD, *WOLD, *WOOLD;

  n = NUMNP;

  R = new double[n];
  Z = new double[n];
  U = new double[n];
  V = new double[n];
  W = new double[n];
  UOLD = new double[n];
  VOLD = new double[n];
  WOLD = new double[n];
  WOOLD = new double[n];

  //stuff for sparse BLAS MV multiplication
  char transa='n';
  double one=1.E0, zero=0.E0;
  char matdescra[6];
  //matdescra[0]='s'; //s = symmetric
  matdescra[0]='g'; //g = generic
  matdescra[1]='u';
  matdescra[2]='n';
  matdescra[3]='c';

  residual_percent_complete_old = -1.;

  FILE *convergence_history;

  if((convergence_history = fopen ("convergence_history.txt", "w")) == NULL)
  {
	   input.Com->ninjaCom(ninjaComClass::ninjaFailure, "A convergence_history file to write to cannot be created.\nIt may be in use by another program.");
	   exit(0);
  }
  fprintf(convergence_history,"\nIteration\tResidual\tResidual_check");

  Preconditioner precond;

  if(precond.initialize(n, A, row_ptr, col_ind, precond.Jacobi, matdescra)==false)
  {
	  input.Com->ninjaCom(ninjaComClass::ninjaWarning, "Initialization of Jacobi preconditioner failed, trying SSOR preconditioner...");
	  if(precond.initialize(NUMNP, A, row_ptr, col_ind, precond.SSOR, matdescra)==false) // SSOR does not work for full asymmetric matrix !!
	  {
		  input.Com->ninjaCom(ninjaComClass::ninjaFailure, "Initialization of SSOR preconditioner failed, CANNOT SOLVE FOR WINDFLOW!!!");
		  return false;
	  }
  }

  //ksp->its = 0;

  for(j=0;j<n;j++)	UOLD[j] = 0.0;	//  u_old  <-   0
  cblas_dcopy(n, UOLD, 1, VOLD, 1);	//	v_old  <-   0
  cblas_dcopy(n, UOLD, 1, W, 1);	//	w      <-   0
  cblas_dcopy(n, UOLD, 1, WOLD, 1);	//	w_old  <-   0

  mkl_dcsrmv(&transa, &n, &n, &one, matdescra, A, col_ind, row_ptr, &row_ptr[1], x, &zero, R); // r <- b - A*x

  for(j=0;j<n;j++)	R[j] = b[j] - R[j];

  bnorm = cblas_dnrm2(n, b, 1);

  precond.solve(R, Z, row_ptr, col_ind);	//apply preconditioner    M*z = r

  //KSP_PCApply(ksp,R,Z);

  dp = cblas_ddot(n, R, 1, Z, 1);
  if(dp<0.0)
  {
	input.Com->ninjaCom(ninjaComClass::ninjaFailure, "ERROR IN SOLVER!!! SQUARE ROOT OF A NEGATIVE NUMBER...");
	return false;
  }

  dp = std::sqrt(dp);
  beta = dp;		//  beta <- sqrt(r'*z)
  eta = beta;

  //  VecCopy(R,V)   V = R
  cblas_dcopy(n, R, 1, V, 1);
  cblas_dcopy(n, Z, 1, U, 1);
  ibeta = 1.0/beta;
  cblas_dscal(n, ibeta, V, 1);	//  v <- r/beta
  cblas_dscal(n, ibeta, U, 1);	//  u <- z/beta

  np = cblas_dnrm2(n, Z, 1);	//  np <- ||z||

  rnorm = np;

  // TEST FOR CONVERGENCE HERE!!!!

  i = 0;

  do{

	  //Lanczos

	  mkl_dcsrmv(&transa, &n, &n, &one, matdescra, A, col_ind, row_ptr, &row_ptr[1], U, &zero, R); // r <- A*x

	  alpha = cblas_ddot(n, U, 1, R, 1);	//  alpha <- r'*u
	  precond.solve(R, Z, row_ptr, col_ind);	//apply preconditioner    M*z = r

	  malpha = -alpha;
	  cblas_daxpy(n, malpha, V, 1, R, 1);	//  r <- r - alpha*v
	  cblas_daxpy(n, malpha, U, 1, Z, 1);	//  z <- z - alpha*u
	  mbeta = -beta;
	  cblas_daxpy(n, mbeta, VOLD, 1, R, 1);	//  r <- r - beta*v_old
	  cblas_daxpy(n, mbeta, UOLD, 1, Z, 1);	//  z <- z - beta*u_old

	  betaold = beta;

	  dp = cblas_ddot(n, R, 1, Z, 1);

	  beta = std::sqrt(dp);		//  beta <- sqrt(r'*z)

	  coold = cold; cold = c; soold = sold; sold = s;

	  rho0 = cold * alpha - coold * sold * betaold;
      rho1 = std::sqrt(rho0*rho0 + beta*beta);
      rho2 = sold * alpha + coold * cold * betaold;
      rho3 = soold * betaold;

	  //  Givens rotation

	  c = rho0 / rho1;
      s = beta / rho1;

	  //  Update

	  cblas_dcopy(n, WOLD, 1, WOOLD, 1);	//  w_oold <- w_old
	  cblas_dcopy(n, W, 1, WOLD, 1);	//  w_old <- w

	  cblas_dcopy(n, U, 1, W, 1);	//  w <- U
	  mrho2 = -rho2;
	  cblas_daxpy(n, mrho2, WOLD, 1, W, 1);	//  w <- w - rho2 w_old
	  mrho3 = -rho3;
	  cblas_daxpy(n, mrho3, WOOLD, 1, W, 1);	//  w <- w - rho3 w_oold
	  irho1 = 1.0 / rho1;
	  cblas_dscal(n, irho1, W, 1);	//  w <- w/rho1

	  ceta = c * eta;
	  cblas_daxpy(n, ceta, W, 1, x, 1);	//  x <- x - ceta w
	  eta = - s * eta;

	  cblas_dcopy(n, V, 1, VOLD, 1);	//  v_old <- v
	  cblas_dcopy(n, U, 1, UOLD, 1);	//  u_old <- u
	  cblas_dcopy(n, R, 1, V, 1);	//  v <- r
	  cblas_dcopy(n, Z, 1, U, 1);	//  u <- z
	  ibeta = 1.0 / beta;
      cblas_dscal(n, ibeta, V, 1);	//  v <- r / beta
	  cblas_dscal(n, ibeta, U, 1);	//  u <- z / beta

	  np = rnorm * fabs(s);

	  rnorm = np;

	  //  Test for convergence
	  //if(np<tol)	break;

	  //if(rnorm<tol*bnorm)   break;

	  //cout<<"residual = "<<np<<endl;

        if(i==1)
            start_resid = np;
	
        if(np<tol)   break;

	  i++;


	  if((i%print_iters)==0)
      {

	    #ifdef NINJA_DEBUG_VERBOSE
		input.Com->ninjaCom(ninjaComClass::ninjaDebug, "solver: n=%d iterations = %d residual norm %12.4e", n,i,rnorm/bnorm);
		//input.Com->ninjaCom(ninjaComClass::ninjaDebug, "Iteration = %d\tResidual = %lf\ttol = %lf", i, resid, tol);
		fprintf(convergence_history,"\n%ld\t%lf\t%lf",i,rnorm/bnorm,tol);
		#endif //NINJA_DEBUG_VERBOSE

		input.Com->ninjaCom(ninjaComClass::ninjaDebug, "solver: n=%d iterations = %d residual norm %12.4e", n,i,rnorm/bnorm);
		fprintf(convergence_history,"\n%d\t%lf\t%lf",i,rnorm/bnorm,tol);


	    residual_percent_complete=100-100*((np-tol)/(start_resid-tol));
		if(residual_percent_complete<residual_percent_complete_old)
			residual_percent_complete=residual_percent_complete_old;
		if(residual_percent_complete<0.)
			 residual_percent_complete=0.;
		else if(residual_percent_complete>100.)
			residual_percent_complete=100.0;

		time_percent_complete=1.8*exp(0.0401*residual_percent_complete);
		residual_percent_complete_old=residual_percent_complete;
                //fprintf(convergence_history,"\n%ld\t%lf\t%lf",i,residual_percent_complete, time_percent_complete);
		input.Com->ninjaCom(ninjaComClass::ninjaSolverProgress, "%d",(int) (time_percent_complete+0.5));
	  }

  }while(i<max_iter);

  if(i >= max_iter)
  {
		input.Com->ninjaCom(ninjaComClass::ninjaFailure, "SOLVER DID NOT CONVERGE WITHIN THE SPECIFIED MAXIMUM NUMBER OF ITERATIONS!!");
		return false;
  }else{
		//input.Com->ninjaCom(ninjaComClass::ninjaDebug, "Solver done.");
		return true;
  }

	fclose(convergence_history);

  if(R)
	delete[] R;
  if(Z)
	delete[] Z;
  if(U)
	delete[] U;
  if(V)
	delete[] V;
  if(W)
	delete[] W;
  if(UOLD)
	delete[] UOLD;
  if(VOLD)
	delete[] VOLD;
  if(WOLD)
	delete[] WOLD;
  if(WOOLD)
	delete[] WOOLD;

  return true;

}

/**@brief Performs the calculation X = alpha * X.
 * X is an N-element vector and alpha is a scalar.
 * A limited version of the BLAS function dscal().
 * @param N Size of vectors.
 * @param X Source vector.
 * @param incX Number of values to skip.  MUST BE 1 FOR THIS VERSION!!
 */
void FiniteElementMethod::cblas_dscal(const int N, const double alpha, double *X, const int incX)
{
     int i, ix;

     if (incX <= 0) return;

     ix = OFFSET(N, incX);

     for (i = 0; i < N; i++) {
         X[ix] *= alpha;
         ix    += incX;
     }
}

/**Copies values from the X vector to the Y vector.
 * A limited version of the BLAS function dcopy().
 * @param N Size of vectors.
 * @param X Source vector.
 * @param incX Number of values to skip.  MUST BE 1 FOR THIS VERSION!!
 * @param Y Target vector to copy values to.
 * @param incY Number of values to skip.  MUST BE 1 FOR THIS VERSION!!
 */
void FiniteElementMethod::cblas_dcopy(const int N, const double *X, const int incX, double *Y, const int incY)
{
	int i;
	for(i=0; i<N; i++)
		Y[i] = X[i];
}

/**Performs the dot product X*Y.
 * A limited version of the BLAS function ddot().
 * @param N Size of vectors.
 * @param X Vector of size N.
 * @param incX Number of values to skip.  MUST BE 1 FOR THIS VERSION!!
 * @param Y Vector of size N.
 * @param incY Number of values to skip.  MUST BE 1 FOR THIS VERSION!!
 * @return Dot product X*Y value.
 */
double FiniteElementMethod::cblas_ddot(const int N, const double *X, const int incX, const double *Y, const int incY)
{
	double val=0.0;
	int i;

	#pragma omp parallel for reduction(+:val)
	for(i=0;i<N;i++)
		val += X[i]*Y[i];

	return val;
}

/**Performs the calculation Y = Y + alpha * X.
 * A limited version of the BLAS function daxpy().
 * @param N Size of vectors.
 * @param alpha
 * @param X Vector of size N.
 * @param incX Number of values to skip.  MUST BE 1 FOR THIS VERSION!!
 * @param Y Vector of size N.
 * @param incY Number of values to skip.  MUST BE 1 FOR THIS VERSION!!
 */
void FiniteElementMethod::cblas_daxpy(const int N, const double alpha, const double *X, const int incX, double *Y, const int incY)
{
	int i;

	#pragma omp parallel for
	for(i=0; i<N; i++)
		Y[i] += alpha*X[i];
}

/**Computes the 2-norm of X.
 * A limited version of the BLAS function dnrm2().
 * @param N Size of X.
 * @param X Vector of size N.
 * @param incX Number of values to skip.  MUST BE 1 FOR THIS VERSION!!
 * @return Value of the 2-norm of X.
 */
double FiniteElementMethod::cblas_dnrm2(const int N, const double *X, const int incX)
{
	double val=0.0;
	int i;

	//#pragma omp parallel for reduction(+:val)
	for(i=0;i<N;i++)
		val += X[i]*X[i];
	val = std::sqrt(val);

	return val;
}

/**
 * @brief Computes the vector-matrix product A*x=y.
 *
 * This is a limited version of the BLAS function dcsrmv().
 * It is limited in that the matrix must be in compressed sparse row
 * format, and symmetric with only the upper triangular part stored.
 * ALPHA=1 and BETA=0 must also be true.
 *
 * @note My version of MKL's compressed sparse row (CSR) matrix vector product function
 * MINE ONLY WORKS FOR A SYMMETRICALLY STORED, UPPER TRIANGULAR MATRIX!!!!!!
 * AND ALPHA==1 AND BETA==0
 *
 * @param transa Not used here, but included to stay with the BLAS.
 * @param m Number of rows in "A" matrix. Must equal k in this implementation.
 * @param k Number of columns in "A" matrix. Must equal m in this implementation.
 * @param alpha Must be one for this implementation.
 * @param matdescra Not used here, but included to stay with the BLAS.
 * @param val This is the "A" array.  Must be in compressed sparse row format, and symmetric with only the upper triangular part stored.
 * @param indx An array describing the column index of "A" (sometimes called "col_ind").
 * @param pntrb A pointer containing indices of "A" of the starting locations of the rows.
 * @param pntre A pointer containg indices of "A" of the ending locations of the rows.
 * @param x Vector of size m (and k) in the A*x=y computation.
 * @param beta Not used here, but included to stay with the BLAS.
 * @param y Vector of size m (and k) in the A*x=y computation.
 */
void FiniteElementMethod::mkl_dcsrmv(char *transa, int *m, int *k, double *alpha, char *matdescra, double *val, int *indx, int *pntrb, int *pntre, double *x, double *beta, double *y)
{	// My version of MKL's compressed sparse row (CSR) matrix vector product function
	// MINE ONLY WORKS FOR A SYMMETRICALLY STORED, UPPER TRIANGULAR MATRIX!!!!!!
	// AND ALPHA==1 AND BETA==0

		//function multiplies a sparse matrix "val" times a vector "x", result is stored in "y"
		//		ie. Ax = y
		//"m" and "k" are equal to the number of rows and columns in "A" (must be equal in mine)
		//"indx" is an array describing the column index of "A" (sometimes called "col_ind")
		//"pntrb" is a pointer containing indices of "A" of the starting locations of the rows
		//"pntre" is a pointer containg indices of "A" of the ending locations of the rows
		int i,j,N;
		N=*m;

    #pragma omp parallel private(i,j)
    {
        #pragma omp for
        for(i=0;i<N;i++)
            y[i]=0.0;

        #pragma omp for
        for(i=0;i<N;i++)
        {
            y[i] += val[pntrb[i]]*x[i];	// diagonal
            for(j=pntrb[i]+1;j<pntre[i];j++)
            {
                y[i] += val[j]*x[indx[j]];
            }
        }
    }	//end parallel region

    for(i=0;i<N;i++)
    {
        for(j=pntrb[i]+1;j<pntre[i];j++)
        {
            {
                y[indx[j]] += val[j]*x[i];
            }
        }
    }
}

/**
 * @brief Computes the vector-matrix product A^T*x=y.
 *
 * This is a modified version of mkl_dcsrmv().
 * It is modiefied to compute the product of the traspose of the matrix and
 * the x vector. ALPHA=1 and BETA=0 must be true.
 *
 * @note My version of MKL's compressed sparse row (CSR) matrix vector product function
 * THIS FUNCTION ONLY WORKS FOR A FULLY STORED MATRIX!!!!!!
 * AND ALPHA==1 AND BETA==0
 *
 * @param transa Not used here, but included to stay with the BLAS.
 * @param m Number of rows in "A" matrix. Must equal k in this implementation.
 * @param k Number of columns in "A" matrix. Must equal m in this implementation.
 * @param alpha Must be one for this implementation.
 * @param matdescra Not used here, but included to stay with the BLAS.
 * @param val This is the "A" array.  Must be in compressed sparse row format, and symmetric with only the upper triangular part stored.
 * @param indx An array describing the column index of "A" (sometimes called "col_ind").
 * @param pntrb A pointer containing indices of "A" of the starting locations of the rows.
 * @param pntre A pointer containg indices of "A" of the ending locations of the rows.
 * @param x Vector of size m (and k) in the A*x=y computation.
 * @param beta Not used here, but included to stay with the BLAS.
 * @param y Vector of size m (and k) in the A*x=y computation.
 */
void FiniteElementMethod::mkl_trans_dcsrmv(char *transa, int *m, int *k, double *alpha, char *matdescra, double *val, int *indx, int *pntrb, int *pntre, double *x, double *beta, double *y)
{
		//function multiplies the traspose of a sparse matrix "val" times a vector "x", result is stored in "y"
		//		ie. A^Tx = y
		//"m" and "k" are equal to the number of rows and columns in "A" (must be equal in mine)
		//"indx" is an array describing the column index of "A" (sometimes called "col_ind")
		//"pntrb" is a pointer containing indices of "A" of the starting locations of the rows
		//"pntre" is a pointer containg indices of "A" of the ending locations of the rows
		int i,j,N;
		N=*m;
		double *temp;

		temp = new double[N];  //vector to store (A[j] * x[j]) for each row in A (rows are really cols in A^T)

		for(int i = 0; i < N; i++){
            temp[i] = 0.0;
		}

    #pragma omp parallel private(i,j)
    {

        #pragma omp for
        for(i=0;i<N;i++){
            y[i]=0.0;
        }

        #pragma omp for
        for(i=0;i<N;i++)
        {
            // calculates A^Tx "in place" (A^T does not physically exist)
            if(matdescra[0] == 'g'){
                for(j=pntrb[i];j<pntre[i];j++) // iterate over all columns
                {
                    temp[indx[j]] += val[j]*x[i];  // for entire row: add element[j] * x[i] to element[j] in temp vector
                }
            }
        }

        cblas_dcopy(N, temp, 1, y, 1); // y <- temp

    }	//end parallel region

    if(temp)
        delete[] temp;
}

/**Function used to write the A matrix and b vector to a file.
 * Used for debugging purposes.
 * @param NUMNP Number of node points.
 * @param A "A" matrix in A*x=b computation. Stored in compressed row storage format.
 * @param col_ind Column index vector for compressed row stored matrix A.
 * @param row_ptr Row pointer vector for compressed row stored matrix A.
 * @param b "b" vector in A*x=b computation.  Size is NUMNP.
 */
void FiniteElementMethod::Write_A_and_b(int NUMNP)
{
    double *A = SK;
    double *b = RHS;

    FILE *A_file;
    FILE *b_file;

    A_file = fopen("A.txt", "w");
	b_file = fopen("b.txt", "w");

	int i,j;

	for(i=0;i<NUMNP;i++)
	{
		for(j=row_ptr[i];j<row_ptr[i+1];j++)
			fprintf(A_file,"%d = %lf , %d\n", j, A[j], col_ind[j]);
		fprintf(b_file,"%d = %lf\n", i, b[i]);
	}

	fclose(A_file);
	fclose(b_file);
}

void FiniteElementMethod::Deallocate()
{
    if(PHI)
    {	
        delete[] PHI;
        PHI=NULL;
    }
    if(SK)
    {	
        delete[] SK;
        SK=NULL;
    }
    if(col_ind)
    {	
        delete[] col_ind;
        col_ind=NULL;
    }
    if(row_ptr)
    {
        delete[] row_ptr;
        row_ptr=NULL;
    }
    if(RHS)
    {	
        delete[] RHS;
        RHS=NULL;
    }
    if(DIAG)
    {	
        delete[] DIAG;
        DIAG=NULL;
    }
}

/**
 * @brief Sets up compressed row storage for the SK array.
 *
 */
void FiniteElementMethod::SetupSKCompressedRowStorage(const Mesh &mesh, WindNinjaInputs &input)
{
    //Set array values to zero----------------------------
    if(PHI == NULL)
        PHI=new double[mesh.NUMNP];

    int interrows=input.dem.get_nRows()-2;
    int intercols=input.dem.get_nCols()-2;
    int interlayers=mesh.nlayers-2;
    int i, ii, j, jj, k, kk, l;

    //NZND is the # of nonzero elements in the SK stiffness array that are stored
    int NZND=(8*8)+(intercols*4+interrows*4+interlayers*4)*12+
         (intercols*interlayers*2+interrows*interlayers*2+intercols*interrows*2)*18+
         (intercols*interrows*interlayers)*27;

    //this is because we will only store the upper half of the SK matrix since it's symmetric
    NZND = (NZND - mesh.NUMNP)/2 + mesh.NUMNP;	

    //This is the final global stiffness matrix in Compressed Row Storage (CRS) and symmetric 
    SK = new double[NZND];

    //This holds the global column number of the corresponding element in the CRS storage
    col_ind=new int[NZND];

    //This holds the element number in the SK array (CRS) of the first non-zero entry for the
    //global row (the "+1" is so we can use the last entry to quit loops; ie. so we know how 
    //many non-zero elements are in the last node)
    row_ptr=new int[mesh.NUMNP+1];

    RHS=new double[mesh.NUMNP]; //This is the final right hand side (RHS) matrix

    int type; //This is the type of node (corner, edge, side, internal)
    int temp, temp1;

#pragma omp parallel for default(shared) private(i)
    for(i=0; i<mesh.NUMNP; i++)
    {
        PHI[i]=0.;
        RHS[i]=0.;
        row_ptr[i]=0;
    }

#pragma omp parallel for default(shared) private(i)
    for(i=0; i<NZND; i++)
    {
        col_ind[i]=0;
        SK[i]=0.;
    }

    int row, col;

    //Set up Compressed Row Storage (CRS) format (only store upper triangular SK matrix)
    //temp stores the location (in the SK and col_ind arrays) where the first non-zero element
    //for the current row is located
    temp=0;
    for(k=0;k<mesh.nlayers;k++)
    {
        for(i=0;i<input.dem.get_nRows();i++)
        {
            for(j=0;j<input.dem.get_nCols();j++) //Looping over all nodes using i,j,k notation
            {
                type=mesh.get_node_type(i,j,k);
                if(type==0) //internal node
                {
                    row = k*input.dem.get_nCols()*input.dem.get_nRows()+i*input.dem.get_nCols()+j;
                    row_ptr[row]=temp;
                    temp1=temp;
                    for(kk=-1;kk<2;kk++)
                    {
                        for(ii=-1;ii<2;ii++)
                        {
                            for(jj=-1;jj<2;jj++)
                            {
                                col = (k+kk)*input.dem.get_nCols()*input.dem.get_nRows()+
                                    (i+ii)*input.dem.get_nCols()+(j+jj);
                                if(col >= row)	//only do if we're on the upper triangular part of SK
                                {
                                    col_ind[temp1]=col;
                                    temp1++;
                                }
                            }
                        }
                    }
                    temp=temp1;
                }
                else if(type==1) //face node
                {
                    row = k*input.dem.get_nCols()*input.dem.get_nRows()+i*input.dem.get_nCols()+j;
                    row_ptr[row]=temp;
                    temp1=temp;
                    for(kk=-1;kk<2;kk++)
                    {
                        for(ii=-1;ii<2;ii++)
                        {
                            for(jj=-1;jj<2;jj++)
                            {
                                if(((i+ii)<0)||((i+ii)>(input.dem.get_nRows()-1)))
                                    continue;
                                if(((j+jj)<0)||((j+jj)>(input.dem.get_nCols()-1)))
                                    continue;
                                if(((k+kk)<0)||((k+kk)>(mesh.nlayers-1)))
                                    continue;

                                col = (k+kk)*input.dem.get_nCols()*input.dem.get_nRows()+
                                    (i+ii)*input.dem.get_nCols()+(j+jj);
                                if(col >= row) //only do if we're on the upper triangular part of SK
                                {
                                    col_ind[temp1]=col;
                                    temp1++;
                                }
                            }
                        }
                    }
                    temp=temp1;
                }
                else if(type==2) //edge node
                {
                    row = k*input.dem.get_nCols()*input.dem.get_nRows()+i*input.dem.get_nCols()+j;
                    row_ptr[row]=temp;
                    temp1=temp;
                    for(kk=-1;kk<2;kk++)
                    {
                        for(ii=-1;ii<2;ii++)
                        {
                            for(jj=-1;jj<2;jj++)
                            {
                                if(((i+ii)<0)||((i+ii)>(input.dem.get_nRows()-1)))
                                    continue;
                                if(((j+jj)<0)||((j+jj)>(input.dem.get_nCols()-1)))
                                    continue;
                                if(((k+kk)<0)||((k+kk)>(mesh.nlayers-1)))
                                    continue;

                                col = (k+kk)*input.dem.get_nCols()*input.dem.get_nRows()+
                                    (i+ii)*input.dem.get_nCols()+(j+jj);
                                if(col >= row) //only do if we're on the upper triangular part of SK
                                {
                                    col_ind[temp1]=col;
                                    temp1++;
                                }
                            }
                        }
                    }
                    //temp=temp+12;
                    temp=temp1;
                }
                else if(type==3) //corner node
                {
                    row = k*input.dem.get_nCols()*input.dem.get_nRows()+i*input.dem.get_nCols()+j;
                    row_ptr[row]=temp;
                    temp1=temp;
                    for(kk=-1;kk<2;kk++)
                    {
                        for(ii=-1;ii<2;ii++)
                        {
                            for(jj=-1;jj<2;jj++)
                            {
                                if(((i+ii)<0)||((i+ii)>(input.dem.get_nRows()-1)))
                                    continue;
                                if(((j+jj)<0)||((j+jj)>(input.dem.get_nCols()-1)))
                                    continue;
                                if(((k+kk)<0)||((k+kk)>(mesh.nlayers-1)))
                                    continue;

                                col = (k+kk)*input.dem.get_nCols()*input.dem.get_nRows()+
                                    (i+ii)*input.dem.get_nCols()+(j+jj);
                                if(col >= row) //only do if we're on the upper triangular part of SK
                                {
                                    col_ind[temp1]=col;
                                    temp1++;
                                }
                            }
                        }
                    }
                    temp=temp1;
                }
                else
                    throw std::logic_error("Error arranging SK array. Exiting...");
            }
        }
    }
    row_ptr[mesh.NUMNP]=temp; //Set last value of row_ptr, so we can use "row_ptr+1" to use to index to in loops
}

void FiniteElementMethod::SetStability(const Mesh &mesh, WindNinjaInputs &input,
                wn_3dVectorField &U0,
                AsciiGrid<double> &CloudGrid,
                boost::shared_ptr<initialize> &init)
{

}

void FiniteElementMethod::ComputeUVWField(const Mesh &mesh, WindNinjaInputs &input,
                    wn_3dVectorField &U0,
                    wn_3dVectorField &U)
{

}
