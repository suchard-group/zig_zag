#include <RcppEigen.h>
// [[Rcpp::depends(RcppEigen)]]

using Eigen::Map;
using Eigen::MatrixXd;
using Eigen::VectorXd;
using Eigen::ArrayXd;
using Eigen::ArrayXXd;


//' Whiten constraints for use in GenerateUnwhitenedSample
//'
//' Transforms constraints of the form Fx+g >= 0 for a target normal distribution
//' into the corresponding constraints for a standard normal.
//'
//' @param constraint_direc F matrix (k-by-d matrix where k is the number of 
//' linear constraints)
//' @param constraint_bound g vector (k dimensional)
//' @param cholesky upper triangular matrix R from cholesky decomposition of 
//' precision or covariance matrix into R^TR
//' @param mean mean of target distribution
//' @param precision boolean for whether parametrization is by precision (true) 
//' or covariance matrix (false)
//' @return List of new constraint directions, the squared row norms of those 
//' constraints (for computational efficiency later), and new bounds
// [[Rcpp::export]]
Rcpp::List WhitenConstraints(const Map<MatrixXd> constraint_direc,
                             const Map<VectorXd> constraint_bound,
                             const Map<MatrixXd> cholesky,
                             const Map<VectorXd> mean,
                             bool precision) {
  if (precision) {
    ArrayXXd direc =  cholesky.transpose().triangularView<Eigen::Lower>().solve(
      constraint_direc.transpose()).transpose().array();
    return Rcpp::List::create(
      Rcpp::_["direc"] = direc, 
      Rcpp::_["direc_rownorm_sq"]= direc.square().rowwise().sum(),
      Rcpp::_["bound"] = constraint_bound + constraint_direc * mean);
  } else {
    ArrayXXd direc =  constraint_direc * cholesky.transpose();
    return Rcpp::List::create(
      Rcpp::_["direc"] = direc, 
      Rcpp::_["direc_rownorm_sq"]=direc.square().rowwise().sum(),
      Rcpp::_["bound"] = constraint_bound + constraint_direc * mean);
  }
}


//' Compute Hamiltonian after specified time.
//'
//' @param position starting position
//' @param momentum starting momentum
//' @param time amount of time the system is run for
//' @return pair of new (position, momentum)
std::pair<VectorXd, VectorXd> Hamiltonian(const VectorXd position, 
                                          const VectorXd momentum, 
                                          const double time) {
  return std::make_pair(momentum * sin(time) + position * cos(time), 
                        momentum * cos(time) - position * sin(time));
}


//' Reflect momentum off of a constraint boundary.
//' 
//' Given a constraint boundary, calculate the momentum as if that boundary 
//' was a wall and there is an elastic collision, and the angle of incidence 
//' equals the angle of reflection.
//'
//' @param momentum starting momentum
//' @param constraint_direc F matrix (k-by-d matrix where k is the number of 
//' linear constraints)
//' @param constraint_row_normsq vector of squared row norms ofr constraint_direc
//' @param bounce_idx integer index of which constraint is being bounced off of
//' @param time amount of time the system is run for
//' @return momentum after bouncing
VectorXd ReflectMomentum(const VectorXd momentum,
                         const Map<MatrixXd> constraint_direc,
                         const Map<VectorXd> constraint_row_normsq,
                         const int bounce_idx) {
  return momentum - 2 * momentum.dot(constraint_direc.row(bounce_idx-1)) /  
    constraint_row_normsq(bounce_idx-1) * constraint_direc.row(bounce_idx-1).transpose();
}


//' Compute when the next bounce occurs and which constraint it occurs on.
//'
//' @param position starting position
//' @param momentum starting momentum
//' @param constraint_direc F matrix (k-by-d matrix where k is the number of 
//' linear constraints)
//' @param constraint_bound g vector (k dimensional)
//' @return pair of new (time until bounce, constraint index corresponding to bounce)
std::pair<double, int> BounceTime(const VectorXd position,
                                  const VectorXd momentum,
                                  const Map<MatrixXd> constraint_direc,
                                  const Map<VectorXd> constraint_bound) {
  
  ArrayXd fa = (constraint_direc * momentum).array();
  ArrayXd fb = (constraint_direc * position).array();
  ArrayXd U = (fa.square() + fb.square()).sqrt();
  // Eigen doesn't have an atan2 function
  ArrayXd phi = -fa.binaryExpr(fb, [] (double a, double b) { return std::atan2(a,b);} );
  double min_time = std::numeric_limits<double>::infinity();
  int constraint_idx = -1;
  for (int i=0; i<constraint_bound.size(); ++i){
    if (U[i]>abs(constraint_bound[i])) {
      double bounce_time = -phi[i] + std::acos(-constraint_bound[i]/ U[i]);
      if (bounce_time < min_time) {
        min_time = bounce_time;
        constraint_idx = i+1;
      }
    }
  }
  return std::make_pair(min_time, constraint_idx);
}


//' Whiten a given position into the standard normal frame.
//'
//' @param position starting position
//' @param momentum starting momentum
//' @param constraint_direc F matrix (k-by-d matrix where k is the number of 
//' linear constraints)
//' @param constraint_bound g vector (k dimensional)
//' @param cholesky upper triangular matrix R from cholesky decomposition of 
//' precision or covariance matrix into R^TR
//' @param mean mean of target distribution
//' @param precision boolean for whether parametrization is by precision (true) 
//' or covariance matrix (false)
//' @return vector of position in standard normal frame
VectorXd WhitenPosition(const Map<VectorXd> position,
                        const Map<MatrixXd> constraint_direc,
                        const Map<VectorXd> constraint_bound,
                        const Map<MatrixXd> cholesky,
                        const Map<VectorXd> mean,
                        bool precision) {
  
  if (precision) {
    return cholesky * (position - mean);
  } else {
    return cholesky.transpose().triangularView<Eigen::Lower>().solve(position-mean);
  }
}

//' Convert a position from standard normal frame back to original frame.
//'
//' @param position starting position
//' @param cholesky upper triangular matrix R from cholesky decomposition of 
//' precision or covariance matrix into R^TR
//' @param mean mean of target distribution
//' @param precision boolean for whether parametrization is by precision (true) 
//' or covariance matrix (false)
//' @return vector of position in original frame
VectorXd UnwhitenPosition(const VectorXd position,
                          const Map<MatrixXd> cholesky,
                          const Map<VectorXd> mean,
                          bool precision) {
  
  if (precision) {
    return cholesky.triangularView<Eigen::Upper>().solve(position) + mean;
  } else {
    return cholesky.transpose() * position + mean;
  }
}

//' Generate a sample from a truncated standard normal distribution
//'
//' @param initial_position starting position
//' @param initial_momentum starting momentum
//' @param constraint_direc F matrix (k-by-d matrix where k is the number of 
//' linear constraints)
//' @param constraint_row_normsq vector of squared row norms ofr constraint_direc
//' @param constraint_bound g vector (k dimensional)
//' @param total_time total time the particle will bounce for
//' @return vector of position in standard normal frame
VectorXd GenerateWhitenedSample(const VectorXd initial_position,
                                const Map<VectorXd> initial_momentum,
                                const Map<MatrixXd> constraint_direc,
                                const Map<VectorXd> constraint_row_normsq,
                                const Map<VectorXd> constraint_bound,
                                double total_time){
  double bounce_time;
  double travelled_time = 0;
  VectorXd position = initial_position;
  VectorXd momentum = initial_momentum;
  while (true) {
    std::pair<double, int> bounce = BounceTime(position,
                                               momentum,
                                               constraint_direc,
                                               constraint_bound);
    if (bounce.first < total_time - travelled_time) {
      bounce_time = bounce.first;
      std::pair<VectorXd, VectorXd> hamiltonian = Hamiltonian(position, momentum, bounce_time);
      position = hamiltonian.first;
      momentum = ReflectMomentum(hamiltonian.second, 
                                 constraint_direc, 
                                 constraint_row_normsq, 
                                 bounce.second);
      travelled_time += bounce_time;
    } else {
      bounce_time = total_time - travelled_time;
      std::pair<VectorXd, VectorXd> hamiltonian = Hamiltonian(position, momentum, bounce_time);
      return hamiltonian.first;
    }
  }
}

//' Generate a sample from a truncated normal distribution.
//' 
//' First "whiten" the constraints and starting position into the standard normal
//' frame, then generate a sample in that frame, and the convert back to the original
//' frame.
//'
//' @param initial_position starting position
//' @param initial_momentum starting momentum
//' @param constraint_direc F matrix (k-by-d matrix where k is the number of 
//' linear constraints)
//' @param constraint_row_normsq vector of squared row norms ofr constraint_direc
//' @param constraint_bound g vector (k dimensional)
//' @param cholesky upper triangular matrix R from cholesky decomposition of 
//' precision or covariance matrix into R^TR
//' @param mean mean of target distribution
//' @param total_time total time the particle will bounce for
//' @param precision boolean for whether parametrization is by precision (true) 
//' or covariance matrix (false)
//' @return vector of position in standard normal frame
// [[Rcpp::export]]
VectorXd GenerateSample(const Map<VectorXd> initial_position,
                        const Map<VectorXd> initial_momentum,
                        const Map<MatrixXd> constraint_direc,
                        const Map<VectorXd> constraint_row_normsq,
                        const Map<VectorXd> constraint_bound,
                        const Map<MatrixXd> cholesky,
                        const Map<VectorXd> mean,
                        double total_time,
                        bool precision){
  VectorXd sample = WhitenPosition(initial_position,
                                   constraint_direc,
                                   constraint_bound,
                                   cholesky,
                                   mean,
                                   precision);
  sample = GenerateWhitenedSample(sample,
                                  initial_momentum,
                                  constraint_direc,
                                  constraint_row_normsq,
                                  constraint_bound,
                                  total_time);
  return UnwhitenPosition(sample, cholesky, mean, precision);
}

