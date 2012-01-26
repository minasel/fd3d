#include "solver.h"

#undef __FUNCT__
#define __FUNCT__ "bicgSymmetric_kernel"
PetscErrorCode bicgSymmetric_kernel(const Mat A, Vec x, const Vec b, const Vec right_precond, const PetscInt max_iter, const PetscReal tol, const Mat HE, GridInfo gi, MonitorIteration monitor)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	Vec r;  // residual for x
	ierr = VecDuplicate(x, &r); CHKERRQ(ierr);
	ierr = MatMult(A, x, r); CHKERRQ(ierr);
	ierr = VecAYPX(r, -1.0, b); CHKERRQ(ierr);  // r = b - A*x

	Vec p;
	ierr = VecDuplicate(x, &p); CHKERRQ(ierr);
	ierr = VecCopy(r, p); CHKERRQ(ierr);  // p = r

	PetscScalar rr;
	ierr = VecTDot(r, r, &rr); CHKERRQ(ierr);  // rr = r^T * r

	PetscReal norm_r, norm_b;
	ierr = VecNorm(r, NORM_INFINITY, &norm_r); CHKERRQ(ierr);
	ierr = VecNorm(b, NORM_INFINITY, &norm_b); CHKERRQ(ierr);

	PetscReal rel_res = norm_r / norm_b;  // relative residual

	Vec Ap;  // A*p
	ierr = VecDuplicate(x, &Ap); CHKERRQ(ierr);

	PetscScalar pAp;  // p^T * Ap
	PetscScalar alpha;  // sr/qAp
	PetscScalar gamma;  // rr_curr / rr_prev

	PetscInt num_iter;
	for (num_iter = 0; (max_iter <= 0 || num_iter < max_iter) && rel_res > tol; ++num_iter) {
		if (monitor != PETSC_NULL) {
			ierr = monitor(VBMedium, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
		}
		ierr = MatMult(A, p, Ap); CHKERRQ(ierr);  // Ap = A*p

		/*
		// The code below shows if Ap != A^T p, even though A is completely numerically symmetric.
		Vec Ap_temp;
		ierr = VecDuplicate(x, &Ap_temp); CHKERRQ(ierr);

		ierr = MatMultTranspose(A, p, Ap_temp); CHKERRQ(ierr);  // Ap = A*p
		ierr = VecAXPY(Ap_temp, -1.0, Ap); CHKERRQ(ierr);
		PetscReal norm_dAp, norm_Ap;
		ierr = VecNorm(Ap_temp, NORM_INFINITY, &norm_dAp); CHKERRQ(ierr);
		ierr = VecNorm(Ap, NORM_INFINITY, &norm_Ap); CHKERRQ(ierr);
		if (norm_dAp > 0) fprintf(stderr, "haha, %e\n", norm_dAp/norm_Ap);
		 */

		ierr = VecTDot(p, Ap, &pAp); CHKERRQ(ierr);  // pAp = p^T * Ap
		alpha = rr / pAp;

		ierr = VecAXPY(x, alpha, p); CHKERRQ(ierr);  // x = x + alpha * p
		ierr = VecAXPY(r, -alpha, Ap); CHKERRQ(ierr);  // r = r - alpha * Ap

		gamma = rr;
		ierr = VecTDot(r, r, &rr); CHKERRQ(ierr);  // rr = r^T * r
		gamma = rr / gamma;  // gamma = rr_curr / rr_prev

		ierr = VecAYPX(p, gamma, r); CHKERRQ(ierr);  // p = r + gamma * p

		ierr = VecNorm(r, NORM_INFINITY, &norm_r); CHKERRQ(ierr);
		rel_res = norm_r / norm_b;
	}
	if (monitor != PETSC_NULL) {
		ierr = monitor(VBCompact, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
	}

	ierr = VecDestroy(&r); CHKERRQ(ierr);
	ierr = VecDestroy(&p); CHKERRQ(ierr);
	ierr = VecDestroy(&Ap); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "bicgSymmetric"
PetscErrorCode bicgSymmetric(const Mat A, Vec x, const Vec b, const Vec right_precond, const Mat HE, GridInfo gi)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	if (gi.verbose_level >= VBMedium) {
		ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "algorithm: BiCG for symmetric matrices\n"); CHKERRQ(ierr);
	}

	ierr = bicgSymmetric_kernel(A, x, b, right_precond, gi.max_iter, gi.tol, HE, gi, monitorAll);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "cgs_kernel"
PetscErrorCode cgs_kernel(const Mat A, Vec x, const Vec b, const Vec right_precond, const PetscInt max_iter, const PetscReal tol, const Mat HE, GridInfo gi, MonitorIteration monitor)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	Vec r;  // residual for x
	ierr = VecDuplicate(x, &r); CHKERRQ(ierr);
	ierr = MatMult(A, x, r); CHKERRQ(ierr);
	ierr = VecAYPX(r, -1.0, b); CHKERRQ(ierr);  // r = b - A*x

	Vec s0;  // residual for y
	ierr = VecDuplicate(x, &s0); CHKERRQ(ierr);

	/** It turned out that it is very important to set s = r for better convergence. */
	ierr = VecCopy(r, s0); CHKERRQ(ierr);
	//ierr = MatMult(A, r, s0); CHKERRQ(ierr);  // Fletcher's choice
	ierr = VecConjugate(s0); CHKERRQ(ierr);  // this makes s0^T * r = conj(r0)^T * r = <r0, r>

	Vec p;
	ierr = VecDuplicate(x, &p); CHKERRQ(ierr);
	ierr = VecZeroEntries(p); CHKERRQ(ierr);

	Vec q;
	ierr = VecDuplicate(x, &q); CHKERRQ(ierr);
	ierr = VecZeroEntries(q); CHKERRQ(ierr);

	Vec u;
	ierr = VecDuplicate(x, &u); CHKERRQ(ierr);

	PetscReal norm_r, norm_b;
	ierr = VecNorm(r, NORM_INFINITY, &norm_r); CHKERRQ(ierr);
	ierr = VecNorm(b, NORM_INFINITY, &norm_b); CHKERRQ(ierr);

	PetscReal rel_res = norm_r / norm_b;  // relative residual

	PetscScalar s0r = 1.0;  // s0^T * r

	Vec Ap;  // A*p
	ierr = VecDuplicate(x, &Ap); CHKERRQ(ierr);

	PetscScalar s0Ap;  // s0^T * Ap
	PetscScalar alpha;  // s0r/s0Ap
	PetscScalar gamma;  // s0r_curr / s0r_prev

	/*
	   PetscReal norm;
	   ierr = VecNorm(Ap, NORM_INFINITY, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(p, NORM_INFINITY, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(p) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(q, NORM_INFINITY, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(b) = %e\n", norm_b); CHKERRQ(ierr);
	 */
	PetscInt num_iter;
	for (num_iter = 0; (max_iter <= 0 || num_iter < max_iter) && rel_res > tol; ++num_iter) {
		if (monitor != PETSC_NULL) {
			ierr = monitor(VBMedium, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
		}
		gamma = 1.0/s0r;
		ierr = VecTDot(s0, r, &s0r); CHKERRQ(ierr);
		gamma *= s0r;

		ierr = VecWAXPY(u, gamma, q, r); CHKERRQ(ierr);  // u = r + gamma * q
		ierr = VecAXPY(q, gamma, p); CHKERRQ(ierr);  // q = q + gamma * p
		ierr = VecWAXPY(p, gamma, q, u); CHKERRQ(ierr);  // p = u + gamma * (q + gamma * p)

		ierr = MatMult(A, p, Ap); CHKERRQ(ierr);  // Ap = A*p
		ierr = VecTDot(s0, Ap, &s0Ap); CHKERRQ(ierr);  // s0Ap = s0^T * Ap

		//ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "s0r = %e + i %e, s0Ap = %e + i %e\n", PetscRealPart(s0r), PetscImaginaryPart(s0r), PetscRealPart(s0Ap), PetscImaginaryPart(s0Ap)); CHKERRQ(ierr);  // should give the same values as the corresponding values in BiCG

		alpha = s0r / s0Ap;

		ierr = VecWAXPY(q, -alpha, Ap, u); CHKERRQ(ierr);  // q = u - alpha * Ap
		ierr = VecAXPY(u, 1.0, q); CHKERRQ(ierr);  // u' = u + q
		ierr = VecScale(u, -alpha); CHKERRQ(ierr);  // u' = -alpha * u'
		ierr = VecAXPY(x, -1.0, u); CHKERRQ(ierr);  // x = x + alpha * (u + q)
		ierr = MatMultAdd(A, u, r, r); CHKERRQ(ierr);  // r = r - alpha * A * (u + q)

		ierr = VecNorm(r, NORM_INFINITY, &norm_r); CHKERRQ(ierr);
		rel_res = norm_r / norm_b;

		/*
		   ierr = VecNorm(Ap, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "\nnorm(Ap) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		 */
	}
	if (monitor != PETSC_NULL) {
		ierr = monitor(VBCompact, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
	}

	//ierr = VecDestroy(&y); CHKERRQ(ierr);
	ierr = VecDestroy(&r); CHKERRQ(ierr);
	ierr = VecDestroy(&s0); CHKERRQ(ierr);
	ierr = VecDestroy(&p); CHKERRQ(ierr);
	ierr = VecDestroy(&q); CHKERRQ(ierr);
	ierr = VecDestroy(&u); CHKERRQ(ierr);
	ierr = VecDestroy(&Ap); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "cgs"
PetscErrorCode cgs(const Mat A, Vec x, const Vec b, const Vec right_precond, const Mat HE, GridInfo gi)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	if (gi.verbose_level >= VBMedium) {
		ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "algorithm: CGS for asymmetric matrices\n"); CHKERRQ(ierr);
	}

	ierr = cgs_kernel(A, x, b, right_precond, gi.max_iter, gi.tol, HE, gi, monitorAll);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "bicg_kernel"
PetscErrorCode bicg_kernel(const Mat A, Vec x, const Vec b, const Vec right_precond, const PetscInt max_iter, const PetscReal tol, const Mat HE, GridInfo gi, MonitorIteration monitor)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	/*
	   Vec y;
	   ierr = VecDuplicate(x, &y); CHKERRQ(ierr);
	   ierr = VecCopy(x, y); CHKERRQ(ierr);
	 */

	Vec r;  // residual for x
	ierr = VecDuplicate(x, &r); CHKERRQ(ierr);
	ierr = MatMult(A, x, r); CHKERRQ(ierr);
	ierr = VecAYPX(r, -1.0, b); CHKERRQ(ierr);  // r = b - A*x

	Vec s;  // residual for y
	ierr = VecDuplicate(x, &s); CHKERRQ(ierr);
	/*
	//ierr = MatMultTranspose(A, y, s); CHKERRQ(ierr);
	ierr = MatMult(A, y, s); CHKERRQ(ierr);
	ierr = VecAYPX(s, -1.0, b); CHKERRQ(ierr);  // s = b - (A^T)*y
	 */
	/** It turned out that it is very important to set s = r for better convergence. */
	ierr = VecCopy(r, s); CHKERRQ(ierr);
	//ierr = MatMult(A, r, s); CHKERRQ(ierr);  // Fletcher's choice
	ierr = VecConjugate(s); CHKERRQ(ierr);  // this makes s^T * r = conj(r)^T * r = <r, r>

	Vec p;
	ierr = VecDuplicate(x, &p); CHKERRQ(ierr);
	ierr = VecCopy(r, p); CHKERRQ(ierr);  // p = r

	Vec q;
	ierr = VecDuplicate(x, &q); CHKERRQ(ierr);
	ierr = VecCopy(s, q); CHKERRQ(ierr);  // q = s

	PetscReal norm_r, norm_b;
	ierr = VecNorm(r, NORM_INFINITY, &norm_r); CHKERRQ(ierr);
	ierr = VecNorm(b, NORM_INFINITY, &norm_b); CHKERRQ(ierr);

	PetscReal rel_res = norm_r / norm_b;  // relative residual

	PetscScalar sr;  // s^T * r
	ierr = VecTDot(s, r, &sr); CHKERRQ(ierr);

	Vec Ap;  // A*p
	ierr = VecDuplicate(x, &Ap); CHKERRQ(ierr);

	Vec Aq;  // A^T * q
	ierr = VecDuplicate(x, &Aq); CHKERRQ(ierr);

	PetscScalar qAp;  // q^T * Ap
	PetscScalar alpha;  // sr/qAp
	PetscScalar gamma;  // sr_curr / sr_prev

	/*
	   PetscReal norm;
	   ierr = VecNorm(Ap, NORM_INFINITY, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(p, NORM_INFINITY, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(p) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(q, NORM_INFINITY, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(b) = %e\n", norm_b); CHKERRQ(ierr);
	 */
	PetscInt num_iter;
	for (num_iter = 0; (max_iter <= 0 || num_iter < max_iter) && rel_res > tol; ++num_iter) {
		if (monitor != PETSC_NULL) {
			ierr = monitor(VBMedium, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
		}
		ierr = MatMult(A, p, Ap); CHKERRQ(ierr);  // Ap = A*p
		//ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		/*
		   ierr = VecNorm(Ap, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = MatMultTranspose(A, q, Aq); CHKERRQ(ierr);  // Aq = A^T * q
		/*
		   ierr = VecNorm(Aq, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Aq) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = VecTDot(q, Ap, &qAp); CHKERRQ(ierr);  // qAp = q^T * Ap

		//ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "sr = %e + i %e, qAp = %e + i %e\n", PetscRealPart(sr), PetscImaginaryPart(sr), PetscRealPart(qAp), PetscImaginaryPart(qAp)); CHKERRQ(ierr);  // should give the same values as the corresponding values in CGS

		alpha = sr / qAp;

		ierr = VecAXPY(x, alpha, p); CHKERRQ(ierr);  // x = x + alpha * p

		ierr = VecAXPY(r, -alpha, Ap); CHKERRQ(ierr);  // r = r - alpha * Ap
		ierr = VecAXPY(s, -alpha, Aq); CHKERRQ(ierr);  // s = s - alpha * Aq

		gamma = sr;
		ierr = VecTDot(s, r, &sr); CHKERRQ(ierr);  // sr = s^T * r
		gamma = sr / gamma;  // gamma = sr_curr / sr_prev

		ierr = VecAYPX(p, gamma, r); CHKERRQ(ierr);  // p = r + gamma * p
		ierr = VecAYPX(q, gamma, s); CHKERRQ(ierr);  // q = s + gamma * q

		ierr = VecNorm(r, NORM_INFINITY, &norm_r); CHKERRQ(ierr);
		rel_res = norm_r / norm_b;

		/*
		   ierr = VecNorm(Ap, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "\nnorm(Ap) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		 */
	}
	if (monitor != PETSC_NULL) {
		ierr = monitor(VBCompact, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
	}

	//ierr = VecDestroy(&y); CHKERRQ(ierr);
	ierr = VecDestroy(&r); CHKERRQ(ierr);
	ierr = VecDestroy(&s); CHKERRQ(ierr);
	ierr = VecDestroy(&p); CHKERRQ(ierr);
	ierr = VecDestroy(&q); CHKERRQ(ierr);
	ierr = VecDestroy(&Ap); CHKERRQ(ierr);
	ierr = VecDestroy(&Aq); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "bicg_kernel_H"
/**
 * Use Hermitian transpose rather than transpose.
 */
PetscErrorCode bicg_kernel_H(const Mat A, Vec x, const Vec b, const Vec right_precond, const PetscInt max_iter, const PetscReal tol, const Mat HE, GridInfo gi, MonitorIteration monitor)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	/*
	   Vec y;
	   ierr = VecDuplicate(x, &y); CHKERRQ(ierr);
	   ierr = VecCopy(x, y); CHKERRQ(ierr);
	 */

	Vec r;  // residual for x
	ierr = VecDuplicate(x, &r); CHKERRQ(ierr);
	ierr = MatMult(A, x, r); CHKERRQ(ierr);
	ierr = VecAYPX(r, -1.0, b); CHKERRQ(ierr);  // r = b - A*x

	Vec s;  // residual for y
	ierr = VecDuplicate(x, &s); CHKERRQ(ierr);
	/*
	//ierr = MatMultTranspose(A, y, s); CHKERRQ(ierr);
	ierr = MatMult(A, y, s); CHKERRQ(ierr);
	ierr = VecAYPX(s, -1.0, b); CHKERRQ(ierr);  // s = b - (A^T)*y
	 */
	/** It turned out that it is very important to set s = r for better convergence. */
	ierr = VecCopy(r, s); CHKERRQ(ierr);
	//ierr = MatMult(A, r, s); CHKERRQ(ierr);  // Fletcher's choice
	//ierr = VecConjugate(s); CHKERRQ(ierr);

	Vec p;
	ierr = VecDuplicate(x, &p); CHKERRQ(ierr);
	ierr = VecCopy(r, p); CHKERRQ(ierr);  // p = r

	Vec q;
	ierr = VecDuplicate(x, &q); CHKERRQ(ierr);
	ierr = VecCopy(s, q); CHKERRQ(ierr);  // q = s

	PetscReal norm_r, norm_b;
	ierr = VecNorm(r, NORM_INFINITY, &norm_r); CHKERRQ(ierr);
	ierr = VecNorm(b, NORM_INFINITY, &norm_b); CHKERRQ(ierr);

	PetscReal rel_res = norm_r / norm_b;  // relative residual

	PetscScalar sr;  // s^H * r
	ierr = VecDot(s, r, &sr); CHKERRQ(ierr);

	Vec Ap;  // A*p
	ierr = VecDuplicate(x, &Ap); CHKERRQ(ierr);

	Vec Aq;  // A^H * q
	ierr = VecDuplicate(x, &Aq); CHKERRQ(ierr);

	PetscScalar qAp;  // q^H * Ap
	PetscScalar alpha;  // sr/qAp
	PetscScalar gamma;  // sr_curr / sr_prev

	/*
	   PetscReal norm;
	   ierr = VecNorm(Ap, NORM_INFINITY, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(p, NORM_INFINITY, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(p) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(q, NORM_INFINITY, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(b) = %e\n", norm_b); CHKERRQ(ierr);
	 */
	PetscInt num_iter;
	for (num_iter = 0; (max_iter <= 0 || num_iter < max_iter) && rel_res > tol; ++num_iter) {
		if (monitor != PETSC_NULL) {
			ierr = monitor(VBMedium, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
		}
		ierr = MatMult(A, p, Ap); CHKERRQ(ierr);  // Ap = A*p
		//ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		/*
		   ierr = VecNorm(Ap, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = MatMultHermitianTranspose(A, q, Aq); CHKERRQ(ierr);  // Aq = A^H * q
		/*
		   ierr = VecNorm(Aq, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Aq) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = VecDot(q, Ap, &qAp); CHKERRQ(ierr);  // qAp = q^H * Ap
		/*
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		 */
		alpha = sr / qAp;

		ierr = VecAXPY(x, alpha, p); CHKERRQ(ierr);  // x = x + alpha * p

		ierr = VecAXPY(r, -alpha, Ap); CHKERRQ(ierr);  // r = r - alpha * Ap
		ierr = VecAXPY(s, -PetscConj(alpha), Aq); CHKERRQ(ierr);  // s = s - conj(alpha) * Aq

		gamma = sr;
		ierr = VecDot(s, r, &sr); CHKERRQ(ierr);  // sr = s^H * r
		gamma = sr / gamma;  // gamma = sr_curr / sr_prev

		ierr = VecAYPX(p, gamma, r); CHKERRQ(ierr);  // p = r + gamma * p
		ierr = VecAYPX(q, PetscConj(gamma), s); CHKERRQ(ierr);  // q = s + conj(gamma) * q

		ierr = VecNorm(r, NORM_INFINITY, &norm_r); CHKERRQ(ierr);
		rel_res = norm_r / norm_b;

		/*
		   ierr = VecNorm(Ap, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "\nnorm(Ap) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		 */
	}
	if (monitor != PETSC_NULL) {
		ierr = monitor(VBCompact, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
	}

	//ierr = VecDestroy(&y); CHKERRQ(ierr);
	ierr = VecDestroy(&r); CHKERRQ(ierr);
	ierr = VecDestroy(&s); CHKERRQ(ierr);
	ierr = VecDestroy(&p); CHKERRQ(ierr);
	ierr = VecDestroy(&q); CHKERRQ(ierr);
	ierr = VecDestroy(&Ap); CHKERRQ(ierr);
	ierr = VecDestroy(&Aq); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "bicg"
PetscErrorCode bicg(const Mat A, Vec x, const Vec b, const Vec right_precond, const Mat HE, GridInfo gi)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	if (gi.verbose_level >= VBMedium) {
		ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "algorithm: BiCG for asymmetric matrices\n"); CHKERRQ(ierr);
	}

	//ierr = bicg_kernel(A, x, b, right_precond, gi.max_iter, gi.tol, HE, gi, PETSC_NULL);
	ierr = bicg_kernel(A, x, b, right_precond, gi.max_iter, gi.tol, HE, gi, monitorAll);
	//ierr = bicg_kernel_H(A, x, b, right_precond, gi.max_iter, gi.tol, HE, gi, monitorAll);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "qmr_kernel"
/**
 * QMR algorithm without look-ahead.  
 * This is the implementation of Algorithm 3.1 in Freund and Szeto, A Quasi-minimal residual 
 * squared algorithm for non-Hermitian linear systems, Proc. 1992 Copper Mountain Conf. on 
 * Iterative Methods.
 */
PetscErrorCode qmr_kernel(const Mat A, Vec x, const Vec b, const Vec right_precond, const PetscInt max_iter, const PetscReal tol, const Mat HE, GridInfo gi, MonitorIteration monitor)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	/*
	   Vec y;
	   ierr = VecDuplicate(x, &y); CHKERRQ(ierr);
	   ierr = VecCopy(x, y); CHKERRQ(ierr);
	 */

	Vec r;  // residual for x
	ierr = VecDuplicate(x, &r); CHKERRQ(ierr);
	ierr = MatMult(A, x, r); CHKERRQ(ierr);
	ierr = VecAYPX(r, -1.0, b); CHKERRQ(ierr);  // r = b - A*x

	Vec s;  // residual for y
	ierr = VecDuplicate(x, &s); CHKERRQ(ierr);
	/*
	//ierr = MatMultTranspose(A, y, s); CHKERRQ(ierr);
	ierr = MatMult(A, y, s); CHKERRQ(ierr);
	ierr = VecAYPX(s, -1.0, b); CHKERRQ(ierr);  // s = b - (A^T)*y
	 */
	/** It turned out that it is very important to set s = r for better convergence. */
	ierr = VecCopy(r, s); CHKERRQ(ierr);
	//ierr = MatMult(A, r, s); CHKERRQ(ierr);  // Fletcher's choice
	ierr = VecConjugate(s); CHKERRQ(ierr);  // this makes s^T * r = conj(r)^T * r = <r, r>

	Vec p;
	ierr = VecDuplicate(x, &p); CHKERRQ(ierr);
	ierr = VecCopy(r, p); CHKERRQ(ierr);  // p = r

	Vec q;
	ierr = VecDuplicate(x, &q); CHKERRQ(ierr);
	ierr = VecCopy(s, q); CHKERRQ(ierr);  // q = s

	PetscReal norm_r, norm_b;
	ierr = VecNorm(r, NORM_2, &norm_r); CHKERRQ(ierr);
	ierr = VecNorm(b, NORM_2, &norm_b); CHKERRQ(ierr);

	Vec res;
	ierr = VecDuplicate(x, &res); CHKERRQ(ierr);
	ierr = VecCopy(r, res); CHKERRQ(ierr);  // res = r

	PetscReal norm_res = norm_r;
	PetscReal rel_res = norm_res / norm_b;  // relative residual

	PetscScalar tau = norm_r, theta = 0.0, csq;

	Vec d;
	ierr = VecDuplicate(x, &d); CHKERRQ(ierr);
	ierr = VecZeroEntries(d); CHKERRQ(ierr);  // d = 0

	PetscScalar sr;  // s^T * r
	ierr = VecTDot(s, r, &sr); CHKERRQ(ierr);

	Vec Ap;  // A*p
	ierr = VecDuplicate(x, &Ap); CHKERRQ(ierr);

	Vec Aq;  // A^T * q
	ierr = VecDuplicate(x, &Aq); CHKERRQ(ierr);

	PetscScalar qAp;  // q^T * Ap
	PetscScalar alpha;  // sr/qAp
	PetscScalar gamma;  // sr_curr / sr_prev

	/*
	   PetscReal norm;
	   ierr = VecNorm(Ap, NORM_2, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(p, NORM_2, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(p) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(q, NORM_2, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(b) = %e\n", norm_b); CHKERRQ(ierr);
	 */
	PetscInt num_iter;
	for (num_iter = 0; (max_iter <= 0 || num_iter < max_iter) && rel_res > tol; ++num_iter) {
		if (monitor != PETSC_NULL) {
			ierr = monitor(VBMedium, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
		}
		ierr = MatMult(A, p, Ap); CHKERRQ(ierr);  // Ap = A*p
		//ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		/*
		   ierr = VecNorm(Ap, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = MatMultTranspose(A, q, Aq); CHKERRQ(ierr);  // Aq = A^T * q
		/*
		   ierr = VecNorm(Aq, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Aq) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = VecTDot(q, Ap, &qAp); CHKERRQ(ierr);  // qAp = q^T * Ap
		/*
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		 */
		alpha = sr / qAp;

		ierr = VecAXPY(r, -alpha, Ap); CHKERRQ(ierr);  // r = r - alpha * Ap
		ierr = VecAXPY(s, -alpha, Aq); CHKERRQ(ierr);  // s = s - alpha * Aq

		ierr = VecScale(d, theta*theta); CHKERRQ(ierr);  // d(n) = theta(n-1)^2 * d(n-1)

		ierr = VecNorm(r, NORM_2, &norm_r); CHKERRQ(ierr);
		theta = norm_r/tau;
		csq = 1.0 / (1.0 + theta*theta);
		//csq = 1.0/(theta*theta);
		tau *= (theta * PetscSqrtScalar(csq));

		ierr = VecAXPY(d, alpha, p); CHKERRQ(ierr);  // d(n) = theta(n-1)^2 * d(n-1) + alpha(n-1) * p(n-1)
		ierr = VecScale(d, csq); CHKERRQ(ierr);  // d(n) = c(n)^2 * theta(n-1)^2 * d(n-1) + c(n)^2 * alpha(n-1) * p(n-1)

		ierr = VecAXPY(x, 1.0, d); CHKERRQ(ierr);  // x = x + d

		gamma = sr;
		ierr = VecTDot(s, r, &sr); CHKERRQ(ierr);  // sr = s^T * r
		gamma = sr / gamma;  // gamma = sr_curr / sr_prev


		ierr = VecAYPX(p, gamma, r); CHKERRQ(ierr);  // p = r + gamma * p
		ierr = VecAYPX(q, gamma, s); CHKERRQ(ierr);  // q = s + gamma * q

		/** Since the residual vector is not a byproduct of the QMR iteration, we explicitly
		  evaluate the residual vector. */
		ierr = MatMult(A, x, res); CHKERRQ(ierr);
		ierr = VecAYPX(res, -1.0, b); CHKERRQ(ierr);  // r = b - A*x
		ierr = VecNorm(res, NORM_2, &norm_res); CHKERRQ(ierr);

		rel_res = norm_res / norm_b;
		//rel_res = norm_r / norm_b;

		/*
		   ierr = VecNorm(Ap, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "\nnorm(Ap) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		 */
	}
	if (monitor != PETSC_NULL) {
		ierr = monitor(VBCompact, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
	}

	//ierr = VecDestroy(&y); CHKERRQ(ierr);
	ierr = VecDestroy(&r); CHKERRQ(ierr);
	ierr = VecDestroy(&s); CHKERRQ(ierr);
	ierr = VecDestroy(&p); CHKERRQ(ierr);
	ierr = VecDestroy(&q); CHKERRQ(ierr);
	ierr = VecDestroy(&d); CHKERRQ(ierr);
	ierr = VecDestroy(&res); CHKERRQ(ierr);
	ierr = VecDestroy(&Ap); CHKERRQ(ierr);
	ierr = VecDestroy(&Aq); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "qmr2_kernel"
/**
 * QMR based on coupled recurrences.
 * This is the implementation of Algorithm 7.1 in Freund and Nachtigal, An implementation of the
 * QMR method based on coupled two-term recurrences, SIAM J. Sci. Comput., Vol. 15, No. 2, 1994.
 */
PetscErrorCode qmr2_kernel(const Mat A, Vec x, const Vec b, const Vec right_precond, const PetscInt max_iter, const PetscReal tol, const Mat HE, GridInfo gi, MonitorIteration monitor)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	/*
	   Vec y;
	   ierr = VecDuplicate(x, &y); CHKERRQ(ierr);
	   ierr = VecCopy(x, y); CHKERRQ(ierr);
	 */

	PetscReal norm_b;
	ierr = VecNorm(b, NORM_2, &norm_b); CHKERRQ(ierr);

	Vec r;  // residual for x
	ierr = VecDuplicate(x, &r); CHKERRQ(ierr);
	ierr = MatMult(A, x, r); CHKERRQ(ierr);
	ierr = VecAYPX(r, -1.0, b); CHKERRQ(ierr);  // r = b - A*x

	Vec res;
	ierr = VecDuplicate(x, &res); CHKERRQ(ierr);
	ierr = VecCopy(r, res); CHKERRQ(ierr);  // res = r

	PetscReal norm_r;
	ierr = VecNormalize(r, &norm_r); CHKERRQ(ierr);  // r = r / norm(r)

	PetscReal norm_res = norm_r;
	PetscReal rel_res = norm_res / norm_b;  // relative residual

	Vec s;  // residual for y
	ierr = VecDuplicate(x, &s); CHKERRQ(ierr);
	/*
	//ierr = MatMultTranspose(A, y, s); CHKERRQ(ierr);
	ierr = MatMult(A, y, s); CHKERRQ(ierr);
	ierr = VecAYPX(s, -1.0, b); CHKERRQ(ierr);  // s = b - (A^T)*y
	 */
	/** It turned out that it is very important to set s = r for better convergence. */
	ierr = VecCopy(r, s); CHKERRQ(ierr);
	//ierr = MatMult(A, r, s); CHKERRQ(ierr);  // Fletcher's choice
	ierr = VecConjugate(s); CHKERRQ(ierr);  // this makes s^T * r = conj(r)^T * r = <r, r>

	PetscReal norm_s = 1.0;  // norm(s) = norm(r) = 1.0, since r has been normalized

	Vec p;
	ierr = VecDuplicate(x, &p); CHKERRQ(ierr);
	ierr = VecZeroEntries(p); CHKERRQ(ierr);  // p = 0

	Vec q;
	ierr = VecDuplicate(x, &q); CHKERRQ(ierr);
	ierr = VecZeroEntries(q); CHKERRQ(ierr);  // q = 0

	Vec d;
	ierr = VecDuplicate(x, &d); CHKERRQ(ierr);
	ierr = VecZeroEntries(d); CHKERRQ(ierr);  // d = 0

	PetscScalar eta = -1.0, theta = 0.0, csq = 1.0;

	PetscScalar sr;  // s^T * r
	ierr = VecTDot(s, r, &sr); CHKERRQ(ierr);

	Vec Ap;  // A*p
	ierr = VecDuplicate(x, &Ap); CHKERRQ(ierr);

	Vec Aq;  // A^T * q
	ierr = VecDuplicate(x, &Aq); CHKERRQ(ierr);

	PetscScalar qAp = 1.0;  // q^T * Ap
	PetscScalar beta;  // qAp / sr

	/*
	   PetscReal norm;
	   ierr = VecNorm(Ap, NORM_2, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(p, NORM_2, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(p) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(q, NORM_2, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(b) = %e\n", norm_b); CHKERRQ(ierr);
	 */
	PetscInt num_iter;
	for (num_iter = 0; (max_iter <= 0 || num_iter < max_iter) && rel_res > tol; ++num_iter) {
		if (monitor != PETSC_NULL) {
			ierr = monitor(VBMedium, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
		}
		ierr = VecTDot(s, r, &sr); CHKERRQ(ierr);  // sr = s^T * r

		ierr = VecAYPX(p, -norm_s*sr/qAp, r); CHKERRQ(ierr);  // p = r - (norm_s*sr/qAp) * p
		ierr = VecAYPX(q, -norm_r*sr/qAp, s); CHKERRQ(ierr);  // q = s - (norm_r*sr/qAp) * q

		ierr = MatMult(A, p, Ap); CHKERRQ(ierr);  // Ap = A*p
		//ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		/*
		   ierr = VecNorm(Ap, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = MatMultTranspose(A, q, Aq); CHKERRQ(ierr);  // Aq = A^T * q
		/*
		   ierr = VecNorm(Aq, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Aq) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = VecTDot(q, Ap, &qAp); CHKERRQ(ierr);  // qAp = q^T * Ap
		/*
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		 */
		beta = qAp / sr;

		ierr = VecAYPX(r, -beta, Ap); CHKERRQ(ierr);  // r = Ap - beta * r
		ierr = VecAYPX(s, -beta, Aq); CHKERRQ(ierr);  // s = Aq - beta * s

		eta *= (-norm_r/csq/beta);  // eta(n) = - norm_r(n)/(beta(n)*csq(n-1)) * eta(n-1)

		ierr = VecScale(d, theta*theta); CHKERRQ(ierr);  // d(n) = theta(n-1)^2 * d(n-1)

		ierr = VecNormalize(r, &norm_r); CHKERRQ(ierr);  // r = r / norm(r), norm_r(n+1) = norm(r)
		ierr = VecNormalize(s, &norm_s); CHKERRQ(ierr);  // s = s / norm(s), norm_s(n+1) = norm(s)

		theta = norm_r / (PetscSqrtScalar(csq) * PetscAbsScalar(beta));
		csq = 1.0 / (1.0 + theta*theta);
		eta *= csq;

		ierr = VecScale(d, csq); CHKERRQ(ierr);  // d(n) = c(n)^2 * theta(n-1)^2 * d(n-1)
		ierr = VecAXPY(d, eta, p); CHKERRQ(ierr);  // d(n) = eta(n)*p(n) + c(n)^2 * theta(n-1)^2 * d(n-1)

		ierr = VecAXPY(x, 1.0, d); CHKERRQ(ierr);  // x = x + d

		/** Since the residual vector is not a byproduct of the QMR iteration, we explicitly
		  evaluate the residual vector. */
		ierr = MatMult(A, x, res); CHKERRQ(ierr);
		ierr = VecAYPX(res, -1.0, b); CHKERRQ(ierr);  // r = b - A*x
		ierr = VecNorm(res, NORM_2, &norm_res); CHKERRQ(ierr);

		rel_res = norm_res / norm_b;
		//rel_res = norm_r / norm_b;

		/*
		   ierr = VecNorm(Ap, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "\nnorm(Ap) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		 */
	}
	if (monitor != PETSC_NULL) {
		ierr = monitor(VBCompact, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
	}

	//ierr = VecDestroy(&y); CHKERRQ(ierr);
	ierr = VecDestroy(&r); CHKERRQ(ierr);
	ierr = VecDestroy(&s); CHKERRQ(ierr);
	ierr = VecDestroy(&p); CHKERRQ(ierr);
	ierr = VecDestroy(&q); CHKERRQ(ierr);
	ierr = VecDestroy(&d); CHKERRQ(ierr);
	ierr = VecDestroy(&res); CHKERRQ(ierr);
	ierr = VecDestroy(&Ap); CHKERRQ(ierr);
	ierr = VecDestroy(&Aq); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "qmr3_kernel"
/**
 * QMR algorithm using A^H instead of A^T.
 * This modifies qmr_kernel() so that the left Krylov subspace is generated by multiplying A^H 
 * instead of A^T.  The hope is to have the matrix V, whose columns are the basis vectors of the 
 * right Krylov subspace, is biorthogonal to the matrix W, whose columns are the basis vectors of 
 * the left Krylov subspace, so that W^H V is diagoal.  (qmr_kernel() creates V and W such that 
 * W^T V is diagonal.)
 * This makes V = W for Hermitian A.
 */
PetscErrorCode qmr3_kernel(const Mat A, Vec x, const Vec b, const Vec right_precond, const PetscInt max_iter, const PetscReal tol, const Mat HE, GridInfo gi, MonitorIteration monitor)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	/*
	   Vec y;
	   ierr = VecDuplicate(x, &y); CHKERRQ(ierr);
	   ierr = VecCopy(x, y); CHKERRQ(ierr);
	 */

	Vec r;  // residual for x
	ierr = VecDuplicate(x, &r); CHKERRQ(ierr);
	ierr = MatMult(A, x, r); CHKERRQ(ierr);
	ierr = VecAYPX(r, -1.0, b); CHKERRQ(ierr);  // r = b - A*x

	Vec s;  // residual for y
	ierr = VecDuplicate(x, &s); CHKERRQ(ierr);
	/*
	//ierr = MatMultTranspose(A, y, s); CHKERRQ(ierr);
	ierr = MatMult(A, y, s); CHKERRQ(ierr);
	ierr = VecAYPX(s, -1.0, b); CHKERRQ(ierr);  // s = b - (A^T)*y
	 */
	/** It turned out that it is very important to set s = r for better convergence. */
	ierr = VecCopy(r, s); CHKERRQ(ierr);
	//ierr = MatMult(A, r, s); CHKERRQ(ierr);  // Fletcher's choice
	ierr = VecConjugate(s); CHKERRQ(ierr);  // this makes s^T * r = conj(r)^T * r = <r, r>

	Vec p;
	ierr = VecDuplicate(x, &p); CHKERRQ(ierr);
	ierr = VecCopy(r, p); CHKERRQ(ierr);  // p = r

	Vec q;
	ierr = VecDuplicate(x, &q); CHKERRQ(ierr);
	ierr = VecCopy(s, q); CHKERRQ(ierr);  // q = s

	PetscReal norm_r, norm_b;
	ierr = VecNorm(r, NORM_2, &norm_r); CHKERRQ(ierr);
	ierr = VecNorm(b, NORM_2, &norm_b); CHKERRQ(ierr);

	Vec res;
	ierr = VecDuplicate(x, &res); CHKERRQ(ierr);
	ierr = VecCopy(r, res); CHKERRQ(ierr);  // res = r

	PetscReal norm_res = norm_r;
	PetscReal rel_res = norm_res / norm_b;  // relative residual

	PetscScalar tau = norm_r, theta = 0.0, csq;

	Vec d;
	ierr = VecDuplicate(x, &d); CHKERRQ(ierr);
	ierr = VecZeroEntries(d); CHKERRQ(ierr);  // d = 0

	PetscScalar sr;  // s^T * r
	ierr = VecTDot(s, r, &sr); CHKERRQ(ierr);

	Vec Ap;  // A*p
	ierr = VecDuplicate(x, &Ap); CHKERRQ(ierr);

	Vec Aq;  // A^T * q
	ierr = VecDuplicate(x, &Aq); CHKERRQ(ierr);

	PetscScalar qAp;  // q^T * Ap
	PetscScalar alpha;  // sr/qAp
	PetscScalar gamma;  // sr_curr / sr_prev
	Vec test;
	ierr = VecDuplicate(x, &test); CHKERRQ(ierr);
	PetscReal norm_test;
	PetscScalar orth_test;

	/*
	   PetscReal norm;
	   ierr = VecNorm(Ap, NORM_2, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(p, NORM_2, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(p) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(q, NORM_2, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(b) = %e\n", norm_b); CHKERRQ(ierr);
	 */
	PetscInt num_iter;
	for (num_iter = 0; (max_iter <= 0 || num_iter < max_iter) && rel_res > tol; ++num_iter) {
		ierr = VecCopy(s, test); CHKERRQ(ierr);
		ierr = VecConjugate(test); CHKERRQ(ierr);
		ierr = VecAXPY(test, -1.0, r); CHKERRQ(ierr);
		ierr = VecNorm(test, NORM_2, &norm_test); CHKERRQ(ierr);
		ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "\tnorm(r-s) = %e", norm_test); CHKERRQ(ierr);

		ierr = VecCopy(q, test); CHKERRQ(ierr);
		ierr = VecConjugate(test); CHKERRQ(ierr);
		ierr = VecAXPY(test, -1.0, p); CHKERRQ(ierr);
		ierr = VecNorm(test, NORM_2, &norm_test); CHKERRQ(ierr);
		ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "\tnorm(p-q) = %e\n", norm_test); CHKERRQ(ierr);

		if (monitor != PETSC_NULL) {
			ierr = monitor(VBMedium, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
		}

		ierr = MatMult(A, p, Ap); CHKERRQ(ierr);  // Ap = A*p
		//ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		/*
		   ierr = VecNorm(Ap, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = MatMultTranspose(A, q, Aq); CHKERRQ(ierr);  // Aq = A^T * q
		/*
		   ierr = VecNorm(Aq, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Aq) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = VecTDot(q, Ap, &qAp); CHKERRQ(ierr);  // qAp = q^T * Ap
		/*
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		 */
		alpha = sr / qAp;

		ierr = VecCopy(r, test); CHKERRQ(ierr);

		ierr = VecAXPY(r, -alpha, Ap); CHKERRQ(ierr);  // r = r - alpha * Ap
		ierr = VecAXPY(s, -PetscConj(alpha), Aq); CHKERRQ(ierr);  // s = s - alpha * Aq

		ierr = VecDot(test, r, &orth_test); CHKERRQ(ierr);
		ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "\tr(i)^H * r(i+1) = %e\n", PetscAbsScalar(orth_test)/norm_r); CHKERRQ(ierr);

		ierr = VecScale(d, theta*theta); CHKERRQ(ierr);  // d(n) = theta(n-1)^2 * d(n-1)

		ierr = VecNorm(r, NORM_2, &norm_r); CHKERRQ(ierr);
		theta = norm_r/tau;
		csq = 1.0 / (1.0 + theta*theta);
		//csq = 1.0/(theta*theta);
		tau *= (theta * PetscSqrtScalar(csq));

		ierr = VecAXPY(d, alpha, p); CHKERRQ(ierr);  // d(n) = theta(n-1)^2 * d(n-1) + alpha(n-1) * p(n-1)
		ierr = VecScale(d, csq); CHKERRQ(ierr);  // d(n) = c(n)^2 * theta(n-1)^2 * d(n-1) + c(n)^2 * alpha(n-1) * p(n-1)

		ierr = VecAXPY(x, 1.0, d); CHKERRQ(ierr);  // x = x + d

		gamma = sr;
		ierr = VecTDot(s, r, &sr); CHKERRQ(ierr);  // sr = s^T * r
		gamma = sr / gamma;  // gamma = sr_curr / sr_prev


		ierr = VecCopy(p, test); CHKERRQ(ierr);

		ierr = VecAYPX(p, gamma, r); CHKERRQ(ierr);  // p = r + gamma * p
		ierr = VecAYPX(q, PetscConj(gamma), s); CHKERRQ(ierr);  // q = s + gamma * q

		ierr = VecDot(test, p, &orth_test); CHKERRQ(ierr);
		ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "\tp(i)^H * p(i+1) = %e\n", PetscAbsScalar(orth_test)); CHKERRQ(ierr);

		/** Since the residual vector is not a byproduct of the QMR iteration, we explicitly
		  evaluate the residual vector. */
		ierr = MatMult(A, x, res); CHKERRQ(ierr);
		ierr = VecAYPX(res, -1.0, b); CHKERRQ(ierr);  // r = b - A*x
		ierr = VecNorm(res, NORM_2, &norm_res); CHKERRQ(ierr);

		rel_res = norm_res / norm_b;
		//rel_res = norm_r / norm_b;

		/*
		   ierr = VecNorm(Ap, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "\nnorm(Ap) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		 */
	}
	if (monitor != PETSC_NULL) {
		ierr = monitor(VBCompact, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
	}

	//ierr = VecDestroy(&y); CHKERRQ(ierr);
	ierr = VecDestroy(&r); CHKERRQ(ierr);
	ierr = VecDestroy(&s); CHKERRQ(ierr);
	ierr = VecDestroy(&p); CHKERRQ(ierr);
	ierr = VecDestroy(&q); CHKERRQ(ierr);
	ierr = VecDestroy(&d); CHKERRQ(ierr);
	ierr = VecDestroy(&res); CHKERRQ(ierr);
	ierr = VecDestroy(&Ap); CHKERRQ(ierr);
	ierr = VecDestroy(&Aq); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "qmr4_kernel"
/**
 * QMR algorithm using A^H instead of A^T.
 * This modifies qmr2_kernel() so that the left Krylov subspace is generated by multiplying A^H 
 * instead of A^T.  The hope is to have the matrix V, whose columns are the basis vectors of the 
 * right Krylov subspace, is biorthogonal to the matrix W, whose columns are the basis vectors of 
 * the left Krylov subspace, so that W^H V is diagoal.  (qmr_kernel() creates V and W such that 
 * W^T V is diagonal.)
 * This makes V = W for Hermitian A.  Since QMR ensures 
 */
PetscErrorCode qmr4_kernel(const Mat A, Vec x, const Vec b, const Vec right_precond, const PetscInt max_iter, const PetscReal tol, const Mat HE, GridInfo gi, MonitorIteration monitor)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	/*
	   Vec y;
	   ierr = VecDuplicate(x, &y); CHKERRQ(ierr);
	   ierr = VecCopy(x, y); CHKERRQ(ierr);
	 */

	PetscReal norm_b;
	ierr = VecNorm(b, NORM_2, &norm_b); CHKERRQ(ierr);

	Vec r;  // residual for x
	ierr = VecDuplicate(x, &r); CHKERRQ(ierr);
	ierr = MatMult(A, x, r); CHKERRQ(ierr);
	ierr = VecAYPX(r, -1.0, b); CHKERRQ(ierr);  // r = b - A*x

	Vec res;
	ierr = VecDuplicate(x, &res); CHKERRQ(ierr);
	ierr = VecCopy(r, res); CHKERRQ(ierr);  // res = r

	PetscReal norm_r;
	ierr = VecNormalize(r, &norm_r); CHKERRQ(ierr);  // r = r / norm(r)

	PetscReal norm_res = norm_r;
	PetscReal rel_res = norm_res / norm_b;  // relative residual

	Vec s;  // residual for y
	ierr = VecDuplicate(x, &s); CHKERRQ(ierr);
	/*
	//ierr = MatMultTranspose(A, y, s); CHKERRQ(ierr);
	ierr = MatMult(A, y, s); CHKERRQ(ierr);
	ierr = VecAYPX(s, -1.0, b); CHKERRQ(ierr);  // s = b - (A^T)*y
	 */
	/** It turned out that it is very important to set s = r for better convergence. */
	ierr = VecCopy(r, s); CHKERRQ(ierr);
	//ierr = MatMult(A, r, s); CHKERRQ(ierr);  // Fletcher's choice
	ierr = VecConjugate(s); CHKERRQ(ierr);  // this makes s^T * r = conj(r)^T * r = <r, r>

	PetscReal norm_s = 1.0;  // norm(s) = norm(r) = 1.0, since r has been normalized

	Vec p;
	ierr = VecDuplicate(x, &p); CHKERRQ(ierr);
	ierr = VecZeroEntries(p); CHKERRQ(ierr);  // p = 0

	Vec q;
	ierr = VecDuplicate(x, &q); CHKERRQ(ierr);
	ierr = VecZeroEntries(q); CHKERRQ(ierr);  // q = 0

	Vec d;
	ierr = VecDuplicate(x, &d); CHKERRQ(ierr);
	ierr = VecZeroEntries(d); CHKERRQ(ierr);  // d = 0

	PetscScalar eta = -1.0, theta = 0.0, csq = 1.0;

	PetscScalar sr;  // s^T * r
	ierr = VecTDot(s, r, &sr); CHKERRQ(ierr);

	Vec Ap;  // A*p
	ierr = VecDuplicate(x, &Ap); CHKERRQ(ierr);

	Vec Aq;  // A^T * q
	ierr = VecDuplicate(x, &Aq); CHKERRQ(ierr);

	PetscScalar qAp = 1.0;  // q^T * Ap
	PetscScalar beta;  // qAp / sr
	Vec test;
	ierr = VecDuplicate(x, &test); CHKERRQ(ierr);
	PetscScalar orth_test;

	/*
	   PetscReal norm;
	   ierr = VecNorm(Ap, NORM_2, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(p, NORM_2, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(p) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(q, NORM_2, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(b) = %e\n", norm_b); CHKERRQ(ierr);
	 */
	PetscInt num_iter;
	for (num_iter = 0; (max_iter <= 0 || num_iter < max_iter) && rel_res > tol; ++num_iter) {
		if (monitor != PETSC_NULL) {
			ierr = monitor(VBMedium, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
		}
		ierr = VecTDot(s, r, &sr); CHKERRQ(ierr);  // sr = s^T * r

		ierr = VecAYPX(p, -norm_s*sr/qAp, r); CHKERRQ(ierr);  // p = r - (norm_s*sr/qAp) * p
		ierr = VecAYPX(q, -norm_r*sr/qAp, s); CHKERRQ(ierr);  // q = s - (norm_r*sr/qAp) * q

		ierr = MatMult(A, p, Ap); CHKERRQ(ierr);  // Ap = A*p
		//ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		/*
		   ierr = VecNorm(Ap, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = MatMultTranspose(A, q, Aq); CHKERRQ(ierr);  // Aq = A^T * q
		/*
		   ierr = VecNorm(Aq, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Aq) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = VecTDot(q, Ap, &qAp); CHKERRQ(ierr);  // qAp = q^T * Ap
		/*
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		 */
		beta = qAp / sr;

		ierr = VecCopy(r, test); CHKERRQ(ierr);

		ierr = VecAYPX(r, -beta, Ap); CHKERRQ(ierr);  // r = Ap - beta * r
		ierr = VecAYPX(s, -beta, Aq); CHKERRQ(ierr);  // s = Aq - beta * s

		eta *= (-norm_r/csq/beta);  // eta(n) = - norm_r(n)/(beta(n)*csq(n-1)) * eta(n-1)

		ierr = VecScale(d, theta*theta); CHKERRQ(ierr);  // d(n) = theta(n-1)^2 * d(n-1)

		ierr = VecNormalize(r, &norm_r); CHKERRQ(ierr);  // r = r / norm(r), norm_r(n+1) = norm(r)
		ierr = VecNormalize(s, &norm_s); CHKERRQ(ierr);  // s = s / norm(s), norm_s(n+1) = norm(s)

		ierr = VecDot(test, r, &orth_test); CHKERRQ(ierr);
		ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "\tr(i)^H * r(i+1) = %e\n", PetscAbsScalar(orth_test)); CHKERRQ(ierr);


		theta = norm_r / (PetscSqrtScalar(csq) * PetscAbsScalar(beta));
		csq = 1.0 / (1.0 + theta*theta);
		eta *= csq;

		ierr = VecScale(d, csq); CHKERRQ(ierr);  // d(n) = c(n)^2 * theta(n-1)^2 * d(n-1)
		ierr = VecAXPY(d, eta, p); CHKERRQ(ierr);  // d(n) = eta(n)*p(n) + c(n)^2 * theta(n-1)^2 * d(n-1)

		ierr = VecAXPY(x, 1.0, d); CHKERRQ(ierr);  // x = x + d

		/** Since the residual vector is not a byproduct of the QMR iteration, we explicitly
		  evaluate the residual vector. */
		ierr = MatMult(A, x, res); CHKERRQ(ierr);
		ierr = VecAYPX(res, -1.0, b); CHKERRQ(ierr);  // r = b - A*x
		ierr = VecNorm(res, NORM_2, &norm_res); CHKERRQ(ierr);

		rel_res = norm_res / norm_b;
		//rel_res = norm_r / norm_b;

		/*
		   ierr = VecNorm(Ap, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "\nnorm(Ap) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_2, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		 */
	}
	if (monitor != PETSC_NULL) {
		ierr = monitor(VBCompact, x, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
	}

	//ierr = VecDestroy(&y); CHKERRQ(ierr);
	ierr = VecDestroy(&r); CHKERRQ(ierr);
	ierr = VecDestroy(&s); CHKERRQ(ierr);
	ierr = VecDestroy(&p); CHKERRQ(ierr);
	ierr = VecDestroy(&q); CHKERRQ(ierr);
	ierr = VecDestroy(&d); CHKERRQ(ierr);
	ierr = VecDestroy(&res); CHKERRQ(ierr);
	ierr = VecDestroy(&Ap); CHKERRQ(ierr);
	ierr = VecDestroy(&Aq); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "qmr"
PetscErrorCode qmr(const Mat A, Vec x, const Vec b, const Vec right_precond, const Mat HE, GridInfo gi)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	if (gi.verbose_level >= VBMedium) {
		ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "algorithm: QMR for asymmetric matrices\n"); CHKERRQ(ierr);
	}

	ierr = qmr_kernel(A, x, b, right_precond, gi.max_iter, gi.tol, HE, gi, monitorAll);
	//ierr = qmr2_kernel(A, x, b, right_precond, gi.max_iter, gi.tol, HE, gi, monitorAll);
	//ierr = qmr3_kernel(A, x, b, right_precond, gi.max_iter, gi.tol, HE, gi, monitorAll);
	//ierr = qmr4_kernel(A, x, b, right_precond, gi.max_iter, gi.tol, HE, gi, monitorAll);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "testOrthogonal"
PetscErrorCode testOrthogonal(GridInfo gi)
{
	PetscFunctionBegin;

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "operateA"
PetscErrorCode operateA(const Mat CH, const Mat CE, const Vec mu, const Vec eps, const PetscReal omega, const Vec x, Vec y)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	ierr = VecCopy(x, y); CHKERRQ(ierr);

	Vec z;
	ierr = VecDuplicate(x, &z); CHKERRQ(ierr);

	ierr = VecPointwiseDivide(z, y, eps); CHKERRQ(ierr);
	ierr = MatMult(CE, z, y); CHKERRQ(ierr);
	ierr = VecPointwiseDivide(z, y, mu); CHKERRQ(ierr);
	ierr = MatMult(CH, z, y); CHKERRQ(ierr);

	ierr = VecAXPY(y, -omega*omega, x); CHKERRQ(ierr);

	ierr = VecDestroy(&z); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "operateAtr"
PetscErrorCode operateAtr(const Mat CH, const Mat CE, const Vec mu, const Vec eps, const PetscReal omega, const Vec x, Vec y)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	ierr = VecCopy(x, y); CHKERRQ(ierr);

	Vec z;
	ierr = VecDuplicate(x, &z); CHKERRQ(ierr);

	ierr = MatMultTranspose(CH, y, z); CHKERRQ(ierr);
	ierr = VecPointwiseDivide(y, z, mu); CHKERRQ(ierr);
	ierr = MatMultTranspose(CE, y, z); CHKERRQ(ierr);
	ierr = VecPointwiseDivide(y, z, eps); CHKERRQ(ierr);

	ierr = VecAXPY(y, -omega*omega, x); CHKERRQ(ierr);

	ierr = VecDestroy(&z); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "multAandAdag"
/**
 * multAandAdagger
 * ---------------
 * [y1; y2] = [0 A; Adag 0] * [x1; x2].
 */
PetscErrorCode multAandAdag(const Mat A, const Mat Adag, const Vec x1, const Vec x2, Vec y1, Vec y2)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	ierr = MatMult(A, x2, y1); CHKERRQ(ierr);
	ierr = MatMult(Adag, x1, y2); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "multAandAdagTranspose"
/**
 * multAandAdaggerTranspose
 * ---------------
 * [y1; y2] = [0 A; Adag 0]^T * [x1; x2] = [0 Adag^T; A^T, 0] * [x1; x2]
 */
PetscErrorCode multAandAdagTranspose(const Mat A, const Mat Adag, const Vec x1, const Vec x2, Vec y1, Vec y2)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	ierr = MatMultTranspose(Adag, x2, y1); CHKERRQ(ierr);
	ierr = MatMultTranspose(A, x1, y2); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "vecDot"
PetscErrorCode vecDot(const Vec x1, const Vec x2, const Vec y1, const Vec y2, PetscScalar *val)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	PetscScalar val1, val2;
	ierr = VecDot(x1, y1, &val1); CHKERRQ(ierr);
	ierr = VecDot(x2, y2, &val2); CHKERRQ(ierr);
	*val = val1 + val2;

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "vecTDot"
PetscErrorCode vecTDot(const Vec x1, const Vec x2, const Vec y1, const Vec y2, PetscScalar *val)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	PetscScalar val1, val2;
	ierr = VecTDot(x1, y1, &val1); CHKERRQ(ierr);
	ierr = VecTDot(x2, y2, &val2); CHKERRQ(ierr);
	*val = val1 + val2;

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "vec2Norm"
PetscErrorCode vec2Norm(const Vec x1, const Vec x2, PetscReal *val)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	PetscReal val1, val2;
	ierr = VecNorm(x1, NORM_2, &val1); CHKERRQ(ierr);
	ierr = VecNorm(x2, NORM_2, &val2); CHKERRQ(ierr);
	*val = PetscSqrtScalar(val1*val1 + val2*val2);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "vecNormalize"
PetscErrorCode vecNormalize(Vec x1, Vec x2, PetscReal *val)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	PetscReal val_temp;
	ierr = vec2Norm(x1, x2, &val_temp); CHKERRQ(ierr);
	ierr = VecScale(x1, 1.0/val_temp); CHKERRQ(ierr);
	ierr = VecScale(x2, 1.0/val_temp); CHKERRQ(ierr);
	if (val != PETSC_NULL) *val = val_temp;

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "cgAandAdag_kernel"
PetscErrorCode cgAandAdag_kernel(const Mat A, const Mat Adag, Vec x1, Vec x2, const Vec b1, const Vec b2, const Vec right_precond, const PetscInt max_iter, const PetscReal tol, const Mat HE, GridInfo gi, MonitorIteration monitor)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	Vec r1, r2;  // residual vector for x
	ierr = VecDuplicate(x1, &r1); CHKERRQ(ierr);
	ierr = VecDuplicate(x2, &r2); CHKERRQ(ierr);
	ierr = multAandAdag(A, Adag, x1, x2, r1, r2); CHKERRQ(ierr);
	ierr = VecAYPX(r1, -1.0, b1); CHKERRQ(ierr);  // r1 = b1 - A*x2
	ierr = VecAYPX(r2, -1.0, b2); CHKERRQ(ierr);  // r2 = b2 - Adag*x1

	Vec p1, p2;
	ierr = VecDuplicate(x1, &p1); CHKERRQ(ierr);
	ierr = VecDuplicate(x2, &p2); CHKERRQ(ierr);
	ierr = VecCopy(r1, p1); CHKERRQ(ierr);  // p1 = r1
	ierr = VecCopy(r2, p2); CHKERRQ(ierr);  // p2 = r2

	PetscScalar rr;
	ierr = vecDot(r1, r2, r1, r2, &rr); CHKERRQ(ierr);  // rr = r^H * r

	PetscReal norm_r, norm_b;
	ierr = vec2Norm(r1, r2, &norm_r); CHKERRQ(ierr);
	ierr = vec2Norm(b1, b2, &norm_b); CHKERRQ(ierr);

	PetscReal rel_res = norm_r / norm_b;  // relative residual

	Vec Bp1, Bp2;  // B = [0 A; Adag 0]
	ierr = VecDuplicate(x1, &Bp1); CHKERRQ(ierr);
	ierr = VecDuplicate(x2, &Bp2); CHKERRQ(ierr);

	PetscScalar pBp;  // p^T * Bp
	PetscScalar alpha;  // rr/pBp
	PetscScalar gamma;  // rr_curr / rr_prev

	PetscInt num_iter;
	for (num_iter = 0; (max_iter <= 0 || num_iter < max_iter) && rel_res > tol; ++num_iter) {
		if (monitor != PETSC_NULL) {
			ierr = monitor(VBMedium, x2, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
		}
		ierr = multAandAdag(A, Adag, p1, p2, Bp1, Bp2); CHKERRQ(ierr);  // Bp = B*p

		/*
		// The code below shows if Ap != A^T p, even though A is completely numerically symmetric.
		Vec Ap_temp;
		ierr = VecDuplicate(x, &Ap_temp); CHKERRQ(ierr);

		ierr = MatMultTranspose(A, p, Ap_temp); CHKERRQ(ierr);  // Ap = A*p
		ierr = VecAXPY(Ap_temp, -1.0, Ap); CHKERRQ(ierr);
		PetscReal norm_dAp, norm_Ap;
		ierr = VecNorm(Ap_temp, NORM_INFINITY, &norm_dAp); CHKERRQ(ierr);
		ierr = VecNorm(Ap, NORM_INFINITY, &norm_Ap); CHKERRQ(ierr);
		if (norm_dAp > 0) fprintf(stderr, "haha, %e\n", norm_dAp/norm_Ap);
		 */

		ierr = vecDot(p1, p2, Bp1, Bp2, &pBp); CHKERRQ(ierr);  // pBp = p^H * Bp
		alpha = rr / pBp;

		ierr = VecAXPY(x1, alpha, p1); CHKERRQ(ierr);  // x1 = x1 + alpha * p1
		ierr = VecAXPY(x2, alpha, p2); CHKERRQ(ierr);  // x2 = x2 + alpha * p2
		ierr = VecAXPY(r1, -alpha, Bp1); CHKERRQ(ierr);  // r1 = r1 - alpha * Bp1
		ierr = VecAXPY(r2, -alpha, Bp2); CHKERRQ(ierr);  // r2 = r2 - alpha * Bp2

		gamma = rr;
		ierr = vecDot(r1, r2, r1, r2, &rr); CHKERRQ(ierr);  // rr = r^T * r
		gamma = rr / gamma;  // gamma = rr_curr / rr_prev

		ierr = VecAYPX(p1, gamma, r1); CHKERRQ(ierr);  // p1 = r1 + gamma * p1
		ierr = VecAYPX(p2, gamma, r2); CHKERRQ(ierr);  // p2 = r2 + gamma * p2

		ierr = vec2Norm(r1, r2, &norm_r); CHKERRQ(ierr);
		rel_res = norm_r / norm_b;

	}
	if (monitor != PETSC_NULL) {
		ierr = monitor(VBCompact, x2, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
	}

	ierr = VecDestroy(&r1); CHKERRQ(ierr);
	ierr = VecDestroy(&r2); CHKERRQ(ierr);
	ierr = VecDestroy(&p1); CHKERRQ(ierr);
	ierr = VecDestroy(&p2); CHKERRQ(ierr);
	ierr = VecDestroy(&Bp1); CHKERRQ(ierr);
	ierr = VecDestroy(&Bp2); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "cgAandAdag"
PetscErrorCode cgAandAdag(const Mat A, const Mat Adag, Vec x1, Vec x2, const Vec b1, const Vec b2, const Vec right_precond, const Mat HE, GridInfo gi)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	if (gi.verbose_level >= VBMedium) {
		ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "algorithm: CG for B = [0 A; A^H; 0]\n"); CHKERRQ(ierr);
	}

	//ierr = cgAandAdag_kernel(A, Adag, x1, x2, b1, b2, right_precond, gi.max_iter, gi.tol, HE, gi, PETSC_NULL);
	ierr = cgAandAdag_kernel(A, Adag, x1, x2, b1, b2, right_precond, gi.max_iter, gi.tol, HE, gi, monitorAll);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "bicgAandAdag_kernel"
PetscErrorCode bicgAandAdag_kernel(const Mat A, const Mat Adag, Vec x1, Vec x2, const Vec b1, const Vec b2, const Vec right_precond, const PetscInt max_iter, const PetscReal tol, const Mat HE, GridInfo gi, MonitorIteration monitor)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	/*
	   Vec y;
	   ierr = VecDuplicate(x, &y); CHKERRQ(ierr);
	   ierr = VecCopy(x, y); CHKERRQ(ierr);
	 */

	Vec r1, r2;  // residual for x
	ierr = VecDuplicate(x1, &r1); CHKERRQ(ierr);
	ierr = VecDuplicate(x2, &r2); CHKERRQ(ierr);
	ierr = multAandAdag(A, Adag, x1, x2, r1, r2); CHKERRQ(ierr);
	ierr = VecAYPX(r1, -1.0, b1); CHKERRQ(ierr);  // r1 = b1 - A*x2
	ierr = VecAYPX(r2, -1.0, b2); CHKERRQ(ierr);  // r2 = b2 - Adag*x1

	Vec s1, s2;  // residual for y
	ierr = VecDuplicate(x1, &s1); CHKERRQ(ierr);
	ierr = VecDuplicate(x2, &s2); CHKERRQ(ierr);
	/*
	//ierr = MatMultTranspose(A, y, s); CHKERRQ(ierr);
	ierr = MatMult(A, y, s); CHKERRQ(ierr);
	ierr = VecAYPX(s, -1.0, b); CHKERRQ(ierr);  // s = b - (A^T)*y
	 */
	/** It turned out that it is very important to set s = r for better convergence. */
	ierr = VecCopy(r1, s1); CHKERRQ(ierr);
	ierr = VecCopy(r2, s2); CHKERRQ(ierr);
	ierr = multAandAdag(A, Adag, r1, r2, s1, s2); CHKERRQ(ierr);  // Fletcher's choice
	ierr = VecConjugate(s1); CHKERRQ(ierr);
	ierr = VecConjugate(s2); CHKERRQ(ierr);

	Vec p1, p2;
	ierr = VecDuplicate(x1, &p1); CHKERRQ(ierr);
	ierr = VecDuplicate(x2, &p2); CHKERRQ(ierr);
	ierr = VecCopy(r1, p1); CHKERRQ(ierr);  // p2 = r2
	ierr = VecCopy(r2, p2); CHKERRQ(ierr);  // p2 = r2

	Vec q1, q2;
	ierr = VecDuplicate(x1, &q1); CHKERRQ(ierr);
	ierr = VecDuplicate(x2, &q2); CHKERRQ(ierr);
	ierr = VecCopy(s1, q1); CHKERRQ(ierr);  // q1 = s1
	ierr = VecCopy(s2, q2); CHKERRQ(ierr);  // q2 = s2

	PetscReal norm_r, norm_b;
	ierr = vec2Norm(r1, r2, &norm_r); CHKERRQ(ierr);
	ierr = vec2Norm(b1, b2, &norm_b); CHKERRQ(ierr);

	PetscReal rel_res = norm_r / norm_b;  // relative residual

	PetscScalar sr;  // s^T * r
	ierr = vecTDot(s1, s2, r1, r2, &sr); CHKERRQ(ierr);

	Vec Bp1, Bp2;  // B*p
	ierr = VecDuplicate(x1, &Bp1); CHKERRQ(ierr);
	ierr = VecDuplicate(x2, &Bp2); CHKERRQ(ierr);

	Vec Bq1, Bq2;  // B^T * q
	ierr = VecDuplicate(x1, &Bq1); CHKERRQ(ierr);
	ierr = VecDuplicate(x2, &Bq2); CHKERRQ(ierr);

	PetscScalar qBp;  // q^T * Bp
	PetscScalar alpha;  // sr/qBp
	PetscScalar gamma;  // sr_curr / sr_prev

	/*
	   PetscReal norm;
	   ierr = VecNorm(Ap, NORM_INFINITY, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(p, NORM_INFINITY, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(p) = %e\n", norm); CHKERRQ(ierr);
	   ierr = VecNorm(q, NORM_INFINITY, &norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
	   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(b) = %e\n", norm_b); CHKERRQ(ierr);
	 */
	PetscInt num_iter;
	for (num_iter = 0; (max_iter <= 0 || num_iter < max_iter) && rel_res > tol; ++num_iter) {
		if (monitor != PETSC_NULL) {
			ierr = monitor(VBMedium, x1, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
		}
		ierr = multAandAdag(A, Adag, p1, p2, Bp1, Bp2); CHKERRQ(ierr);  // Bp = B*p
		//ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		/*
		   ierr = VecNorm(Ap, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Ap) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = multAandAdagTranspose(A, Adag, q1, q2, Bq1, Bq2); CHKERRQ(ierr);  // Bq = B^T * q
		/*
		   ierr = VecNorm(Aq, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(Aq) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		 */
		ierr = vecTDot(q1, q2, Bp1, Bp2, &qBp); CHKERRQ(ierr);  // qBp = q^T * Bp
		/*
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		 */
		alpha = sr / qBp;

		ierr = VecAXPY(x1, alpha, p1); CHKERRQ(ierr);  // x1 = x1 + alpha * p1
		ierr = VecAXPY(x2, alpha, p2); CHKERRQ(ierr);  // x2 = x2 + alpha * p2

		ierr = VecAXPY(r1, -alpha, Bp1); CHKERRQ(ierr);  // r1 = r1 - alpha * Bp1
		ierr = VecAXPY(r2, -alpha, Bp2); CHKERRQ(ierr);  // r2 = r2 - alpha * Bp2
		ierr = VecAXPY(s1, -alpha, Bq1); CHKERRQ(ierr);  // s1 = s1 - alpha * Bq1
		ierr = VecAXPY(s2, -alpha, Bq2); CHKERRQ(ierr);  // s2 = s2 - alpha * Bq2

		gamma = sr;
		ierr = vecTDot(s1, s2, r1, r2, &sr); CHKERRQ(ierr);  // sr = s^T * r
		gamma = sr / gamma;  // gamma = sr_curr / sr_prev

		ierr = VecAYPX(p1, gamma, r1); CHKERRQ(ierr);  // p1 = r1 + gamma * p1
		ierr = VecAYPX(p2, gamma, r2); CHKERRQ(ierr);  // p2 = r2 + gamma * p2
		ierr = VecAYPX(q1, gamma, s1); CHKERRQ(ierr);  // q1 = s1 + gamma * q1
		ierr = VecAYPX(q2, gamma, s2); CHKERRQ(ierr);  // q2 = s2 + gamma * q2

		ierr = vec2Norm(r1, r2, &norm_r); CHKERRQ(ierr);
		rel_res = norm_r / norm_b;

		/*
		   ierr = VecNorm(Ap, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "\nnorm(Ap) = %e\n", norm); CHKERRQ(ierr);
		   ierr = VecNorm(q, NORM_INFINITY, &norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(q) = %e\n", norm); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "qAp = %e\n", qAp); CHKERRQ(ierr);
		   ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "norm(r) = %e\n", norm_r); CHKERRQ(ierr);
		 */
	}
	if (monitor != PETSC_NULL) {
		ierr = monitor(VBCompact, x1, right_precond, num_iter, rel_res, HE, &gi); CHKERRQ(ierr);
	}

	//ierr = VecDestroy(&y); CHKERRQ(ierr);
	ierr = VecDestroy(&r1); CHKERRQ(ierr);
	ierr = VecDestroy(&r2); CHKERRQ(ierr);
	ierr = VecDestroy(&s1); CHKERRQ(ierr);
	ierr = VecDestroy(&s2); CHKERRQ(ierr);
	ierr = VecDestroy(&p1); CHKERRQ(ierr);
	ierr = VecDestroy(&p2); CHKERRQ(ierr);
	ierr = VecDestroy(&q1); CHKERRQ(ierr);
	ierr = VecDestroy(&q2); CHKERRQ(ierr);
	ierr = VecDestroy(&Bp1); CHKERRQ(ierr);
	ierr = VecDestroy(&Bp2); CHKERRQ(ierr);
	ierr = VecDestroy(&Bq1); CHKERRQ(ierr);
	ierr = VecDestroy(&Bq2); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "bicgAandAdag"
PetscErrorCode bicgAandAdag(const Mat A, const Mat Adag, Vec x1, Vec x2, const Vec b1, const Vec b2, const Vec right_precond, const Mat HE, GridInfo gi)
{
	PetscFunctionBegin;
	PetscErrorCode ierr;

	if (gi.verbose_level >= VBMedium) {
		ierr = PetscFPrintf(PETSC_COMM_WORLD, stdout, "algorithm: BiCG for B = [0 A; A^H; 0]\n"); CHKERRQ(ierr);
	}

	ierr = bicgAandAdag_kernel(A, Adag, x1, x2, b1, b2, right_precond, gi.max_iter, gi.tol, HE, gi, PETSC_NULL);
	//ierr = bicgAandAdag_kernel(A, Adag, x1, x2, b1, b2, right_precond, gi.max_iter, gi.tol, HE, gi, monitorAll);

	PetscFunctionReturn(0);
}