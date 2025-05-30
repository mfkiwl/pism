/* Copyright (C) 2019, 2020, 2021, 2022, 2023, 2024 PISM Authors
 *
 * This file is part of PISM.
 *
 * PISM is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * PISM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PISM; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <algorithm> // std::min, std::max
#include <cmath>     // std::pow

#include "pism/fracturedensity/FractureDensity.hh"
#include "pism/geometry/Geometry.hh"
#include "pism/stressbalance/StressBalance.hh"
#include "pism/util/pism_utilities.hh"

namespace pism {

FractureDensity::FractureDensity(std::shared_ptr<const Grid> grid,
                                 std::shared_ptr<const rheology::FlowLaw> flow_law)
    : Component(grid),
      m_density(grid, "fracture_density"),
      m_density_new(grid, "new_fracture_density"),
      m_growth_rate(grid, "fracture_growth_rate"),
      m_healing_rate(grid, "fracture_healing_rate"),
      m_flow_enhancement(grid, "fracture_flow_enhancement"),
      m_age(grid, "fracture_age"),
      m_age_new(grid, "new_fracture_age"),
      m_toughness(grid, "fracture_toughness"),
      m_strain_rates(grid, "strain_rates", array::WITHOUT_GHOSTS),
      m_deviatoric_stresses(grid, "sigma", array::WITHOUT_GHOSTS, 3),
      m_velocity(grid, "ghosted_velocity"),
      m_flow_law(flow_law) {

  m_density.metadata(0).long_name("fracture density in ice shelf").units("1");
  m_density.metadata()["valid_max"] = { 1.0 };
  m_density.metadata()["valid_min"] = { 0.0 };

  m_growth_rate.metadata(0)
      .long_name("fracture growth rate")
      .units("second^-1");
  m_growth_rate.metadata()["valid_min"] = { 0.0 };

  m_healing_rate.metadata(0)
      .long_name("fracture healing rate")
      .units("second^-1");

  m_flow_enhancement.metadata(0)
      .long_name("fracture-induced flow enhancement");

  m_age.metadata(0)
      .long_name("age since fracturing")
      .units("seconds");

  m_toughness.metadata(0)
      .long_name("fracture toughness")
      .units("Pa");

  m_strain_rates.metadata(0).set_name("eigen1");
  m_strain_rates.metadata(0)
      .long_name("major principal component of horizontal strain-rate")
      .units("second^-1");

  m_strain_rates.metadata(1).set_name("eigen2");
  m_strain_rates.metadata(1)
      .long_name("minor principal component of horizontal strain-rate")
      .units("second^-1");

  m_deviatoric_stresses.metadata(0).set_name("sigma_xx");
  m_deviatoric_stresses.metadata(0).long_name("deviatoric stress in x direction").units("Pa");

  m_deviatoric_stresses.metadata(1).set_name("sigma_yy");
  m_deviatoric_stresses.metadata(1).long_name("deviatoric stress in y direction").units("Pa");

  m_deviatoric_stresses.metadata(2).set_name("sigma_xy");
  m_deviatoric_stresses.metadata(2).long_name("deviatoric shear stress").units("Pa");
}

void FractureDensity::restart(const File &input_file, int record) {
  m_log->message(2, "* Restarting the fracture density model from %s...\n",
                 input_file.name().c_str());

  m_density.read(input_file, record);
  m_age.read(input_file, record);

  regrid("Fracture density model", m_density, REGRID_WITHOUT_REGRID_VARS);
  regrid("Fracture density model", m_age, REGRID_WITHOUT_REGRID_VARS);
}

void FractureDensity::bootstrap(const File &input_file) {
  m_log->message(2, "* Bootstrapping the fracture density model from %s...\n",
                 input_file.name().c_str());

  m_density.regrid(input_file, io::Default(0.0));
  m_age.regrid(input_file, io::Default(0.0));
}

void FractureDensity::initialize(const array::Scalar &density,
                                 const array::Scalar &age) {
  m_density.copy_from(density);
  m_age.copy_from(age);
}

void FractureDensity::initialize() {
  m_density.set(0.0);
  m_age.set(0.0);
}

void FractureDensity::define_model_state_impl(const File &output) const {
  m_density.define(output, io::PISM_DOUBLE);
  m_age.define(output, io::PISM_DOUBLE);
}

void FractureDensity::write_model_state_impl(const File &output) const {
  m_density.write(output);
  m_age.write(output);
}

void FractureDensity::update(double dt,
                             const Geometry &geometry,
                             const array::Vector &velocity,
                             const array::Scalar &hardness,
                             const array::Scalar &bc_mask) {
  using std::pow;

  const double
    dx = m_grid->dx(),
    dy = m_grid->dy();
  const int
    Mx = m_grid->Mx(),
    My = m_grid->My();

  array::Scalar1
    &A     = m_age;
  array::Scalar
    &D     = m_density,
    &D_new = m_density_new,
    &A_new = m_age_new;

  m_velocity.copy_from(velocity);

  stressbalance::compute_2D_principal_strain_rates(m_velocity,
                                                   geometry.cell_type,
                                                   m_strain_rates);

  stressbalance::compute_2D_stresses(*m_flow_law,
                                     m_velocity,
                                     hardness,
                                     geometry.cell_type,
                                     m_deviatoric_stresses);

  array::AccessScope list{&m_velocity, &m_strain_rates, &m_deviatoric_stresses,
                               &D, &D_new, &geometry.cell_type, &bc_mask, &A, &A_new,
                               &m_growth_rate, &m_healing_rate, &m_flow_enhancement,
                               &m_toughness, &hardness, &geometry.ice_thickness};

  D_new.copy_from(D);

  //options
  /////////////////////////////////////////////////////////
  double soft_residual = m_config->get_number("fracture_density.softening_lower_limit");
  // assume linear response function: E_fr = (1-(1-soft_residual)*phi) -> 1-phi
  //
  // See the following article for more:
  //
  // Albrecht, T. / Levermann, A.
  // Fracture-induced softening for large-scale ice dynamics
  // 2014-04
  //
  // The Cryosphere , Vol. 8, No. 2
  // Copernicus GmbH
  // p. 587-605
  //
  // doi:10.5194/tc-8-587-2014
  //
  // get four options for calculation of fracture density.
  // 1st: fracture growth constant gamma
  // 2nd: fracture initiation stress threshold sigma_cr
  // 3rd: healing rate constant gamma_h
  // 4th: healing strain rate threshold
  //
  // more: T. Albrecht, A. Levermann; Fracture field for large-scale
  // ice dynamics; (2012), Journal of Glaciology, Vol. 58, No. 207,
  // 165-176, DOI: 10.3189/2012JoG11J191.

  double gamma         = m_config->get_number("fracture_density.gamma");
  double initThreshold = m_config->get_number("fracture_density.initiation_threshold");
  double gammaheal     = m_config->get_number("fracture_density.gamma_h");
  double healThreshold = m_config->get_number("fracture_density.healing_threshold");

  m_log->message(3, "PISM-PIK INFO: fracture density is found with parameters:\n"
                    " gamma=%.2f, sigma_cr=%.2f, gammah=%.2f, healing_cr=%.1e and soft_res=%f \n",
                 gamma, initThreshold, gammaheal, healThreshold, soft_residual);

  bool do_fracground = m_config->get_flag("fracture_density.include_grounded_ice");

  double fdBoundaryValue = m_config->get_number("fracture_density.phi0");

  bool constant_healing = m_config->get_flag("fracture_density.constant_healing");

  bool fracture_weighted_healing = m_config->get_flag("fracture_density.fracture_weighted_healing");

  bool max_shear_stress = m_config->get_flag("fracture_density.max_shear_stress");

  bool lefm = m_config->get_flag("fracture_density.lefm");

  bool constant_fd = m_config->get_flag("fracture_density.constant_fd");

  bool fd2d_scheme = m_config->get_flag("fracture_density.fd2d_scheme");

  double glen_exponent = m_flow_law->exponent();

  bool borstad_limit = m_config->get_flag("fracture_density.borstad_limit");

  double minH = m_config->get_number("stress_balance.ice_free_thickness_standard");

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    double tempFD = 0.0;

    double u = m_velocity(i, j).u;
    double v = m_velocity(i, j).v;

    if (fd2d_scheme) {
      if (u >= dx * v / dy and v >= 0.0) { //1
        tempFD = u * (D(i, j) - D(i - 1, j)) / dx + v * (D(i - 1, j) - D(i - 1, j - 1)) / dy;
      } else if (u <= dx * v / dy and u >= 0.0) { //2
        tempFD = u * (D(i, j - 1) - D(i - 1, j - 1)) / dx + v * (D(i, j) - D(i, j - 1)) / dy;
      } else if (u >= -dx * v / dy and u <= 0.0) { //3
        tempFD = -u * (D(i, j - 1) - D(i + 1, j - 1)) / dx + v * (D(i, j) - D(i, j - 1)) / dy;
      } else if (u <= -dx * v / dy and v >= 0.0) { //4
        tempFD = -u * (D(i, j) - D(i + 1, j)) / dx + v * (D(i + 1, j) - D(i + 1, j - 1)) / dy;
      } else if (u <= dx * v / dy and v <= 0.0) { //5
        tempFD = -u * (D(i, j) - D(i + 1, j)) / dx - v * (D(i + 1, j) - D(i + 1, j + 1)) / dy;
      } else if (u >= dx * v / dy and u <= 0.0) { //6
        tempFD = -u * (D(i, j + 1) - D(i + 1, j + 1)) / dx - v * (D(i, j) - D(i, j + 1)) / dy;
      } else if (u <= -dx * v / dy and u >= 0.0) { //7
        tempFD = u * (D(i, j + 1) - D(i - 1, j + 1)) / dx - v * (D(i, j) - D(i, j + 1)) / dy;
      } else if (u >= -dx * v / dy and v <= 0.0) { //8
        tempFD = u * (D(i, j) - D(i - 1, j)) / dx - v * (D(i - 1, j) - D(i - 1, j + 1)) / dy;
      } else {
        m_log->message(3, "######### missing case of angle %f of %f and %f at %d, %d \n",
                       atan(v / u) / M_PI * 180., u * 3e7, v * 3e7, i, j);
      }
    } else {
      tempFD += u * (u < 0 ? D(i + 1, j) - D(i, j) : D(i, j) - D(i - 1, j)) / dx;
      tempFD += v * (v < 0 ? D(i, j + 1) - D(i, j) : D(i, j) - D(i, j - 1)) / dy;
    }

    D_new(i, j) -= tempFD * dt;

    //sources /////////////////////////////////////////////////////////////////
    ///von mises criterion

    double
      txx    = m_deviatoric_stresses(i, j).xx,
      tyy    = m_deviatoric_stresses(i, j).yy,
      txy    = m_deviatoric_stresses(i, j).xy,
      T1     = 0.5 * (txx + tyy) + sqrt(0.25 * pow(txx - tyy, 2) + pow(txy, 2)), //Pa
      T2     = 0.5 * (txx + tyy) - sqrt(0.25 * pow(txx - tyy, 2) + pow(txy, 2)), //Pa
      sigmat = sqrt(pow(T1, 2) + pow(T2, 2) - T1 * T2);


    ///max shear stress criterion (more stringent than von mises)
    if (max_shear_stress) {
      double maxshear = fabs(T1);
      maxshear        = std::max(maxshear, fabs(T2));
      maxshear        = std::max(maxshear, fabs(T1 - T2));

      sigmat = maxshear;
    }

    ///lefm mixed-mode criterion
    if (lefm) {
      double sigmamu = 0.1; //friction coefficient between crack faces

      double sigmac = 0.64 / M_PI; //initial crack depth 20cm

      double sigmabetatest, sigmanor, sigmatau, Kone, Ktwo, KSI, KSImax = 0.0, sigmatetanull;

      for (int l = 46; l <= 90; ++l) { //optimize for various precursor angles beta
        sigmabetatest = l * M_PI / 180.0;

        //rist_sammonds99
        sigmanor = 0.5 * (T1 + T2) - (T1 - T2) * cos(2 * sigmabetatest);
        sigmatau = 0.5 * (T1 - T2) * sin(2 * sigmabetatest);
        //shayam_wu90
        if (sigmamu * sigmanor < 0.0) { //compressive case
          if (fabs(sigmatau) <= fabs(sigmamu * sigmanor)) {
            sigmatau = 0.0;
          } else {
            if (sigmatau > 0) { //coulomb friction opposing sliding
              sigmatau += (sigmamu * sigmanor);
            } else {
              sigmatau -= (sigmamu * sigmanor);
            }
          }
        }

        //stress intensity factors
        Kone = sigmanor * sqrt(M_PI * sigmac); //normal
        Ktwo = sigmatau * sqrt(M_PI * sigmac); //shear

        if (Ktwo == 0.0) {
          sigmatetanull = 0.0;
        } else { //eq15 in hulbe_ledoux10 or eq15 shayam_wu90
          sigmatetanull = -2.0 * atan((sqrt(pow(Kone, 2) + 8.0 * pow(Ktwo, 2)) - Kone) / (4.0 * Ktwo));
        }

        KSI = cos(0.5 * sigmatetanull) *
              (Kone * cos(0.5 * sigmatetanull) * cos(0.5 * sigmatetanull) - 0.5 * 3.0 * Ktwo * sin(sigmatetanull));
        // mode I stress intensity

        KSImax = std::max(KSI, KSImax);
      }
      sigmat = KSImax;
    }

    //////////////////////////////////////////////////////////////////////////////

    // fracture density
    double fdnew = 0.0;
    if (borstad_limit) {
      if (geometry.ice_thickness(i, j) > minH) {
        // mean parameters from paper
        double t0    = initThreshold;
        double kappa = 2.8;

        // effective strain rate
        double e1 = m_strain_rates(i, j).eigen1;
        double e2 = m_strain_rates(i, j).eigen2;
        double ee = sqrt(pow(e1, 2.0) + pow(e2, 2.0) - e1 * e2);

        // threshold for unfractured ice
        double e0 = pow((t0 / hardness(i, j)), glen_exponent);

        // threshold for fractured ice (exponential law)
        double ex = exp((e0 - ee) / (e0 * (kappa - 1)));

        // stress threshold for fractures ice
        double te = t0 * ex;

        // actual effective stress
        double ts = hardness(i, j) * pow(ee, 1.0 / glen_exponent) * (1 - D_new(i, j));

        // fracture formation if threshold is hit
        if (ts > te and ee > e0) {
          // new fracture density:
          fdnew       = 1.0 - (ex * pow((ee / e0), -1 / glen_exponent));
          D_new(i, j) = fdnew;
        }
      }
    } else {
      fdnew = gamma * (m_strain_rates(i, j).eigen1 - 0.0) * (1 - D_new(i, j));
      if (sigmat > initThreshold) {
        D_new(i, j) += fdnew * dt;
      }
    }

    //healing
    double fdheal = gammaheal * std::min(0.0, (m_strain_rates(i, j).eigen1 - healThreshold));
    if (geometry.cell_type.icy(i, j)) {
      if (constant_healing) {
        fdheal = gammaheal * (-healThreshold);
        if (fracture_weighted_healing) {
          D_new(i, j) += fdheal * dt * (1 - D(i, j));
        } else {
          D_new(i, j) += fdheal * dt;
        }
      } else if (m_strain_rates(i, j).eigen1 < healThreshold) {
        if (fracture_weighted_healing) {
          D_new(i, j) += fdheal * dt * (1 - D(i, j));
        } else {
          D_new(i, j) += fdheal * dt;
        }
      }
    }

    // bounding
    D_new(i, j) = pism::clip(D_new(i, j), 0.0, 1.0);

    if (geometry.cell_type.icy(i, j)) {
      //fracture toughness
      m_toughness(i, j) = sigmat;

      // fracture growth rate
      if (sigmat > initThreshold) {
        m_growth_rate(i, j) = fdnew;
        //m_growth_rate(i,j)=gamma*(vPrinStrain1(i,j)-0.0)*(1-D_new(i,j));
      } else {
        m_growth_rate(i, j) = 0.0;
      }

      // fracture healing rate
      if (geometry.cell_type.icy(i, j)) {
        if (constant_healing or (m_strain_rates(i, j).eigen1 < healThreshold)) {
          if (fracture_weighted_healing) {
            m_healing_rate(i, j) = fdheal * (1 - D(i, j));
          } else {
            m_healing_rate(i, j) = fdheal;
          }
        } else {
          m_healing_rate(i, j) = 0.0;
        }
      }

      // fracture age since fracturing occurred
      {
        auto a = A.star(i, j);
        A_new(i, j) -= dt * u * (u < 0 ? a.e - a.c : a.c - a.w) / dx;
        A_new(i, j) -= dt * v * (v < 0 ? a.n - a.c : a.c - a.s) / dy;
        A_new(i, j) += dt;
        if (sigmat > initThreshold) {
          A_new(i, j) = 0.0;
        }
      }

      // additional flow enhancement due to fracture softening
      double softening = pow((1.0 - (1.0 - soft_residual) * D_new(i, j)), -glen_exponent);
      if (geometry.cell_type.icy(i, j)) {
        m_flow_enhancement(i, j) = 1.0 / pow(softening, 1 / glen_exponent);
      } else {
        m_flow_enhancement(i, j) = 1.0;
      }
    }

    // boundary condition
    if (geometry.cell_type.grounded(i, j) and not do_fracground) {

      if (bc_mask(i, j) > 0.5) {
        D_new(i, j) = fdBoundaryValue;

        {
          A_new(i, j)              = 0.0;
          m_growth_rate(i, j)      = 0.0;
          m_healing_rate(i, j)     = 0.0;
          m_flow_enhancement(i, j) = 1.0;
          m_toughness(i, j)        = 0.0;
        }
      }
    }

    // ice free regions and boundary of computational domain
    if (geometry.cell_type.ice_free(i, j) or i == 0 or j == 0 or i == Mx - 1 or j == My - 1) {

      D_new(i, j) = 0.0;

      {
        A_new(i, j)              = 0.0;
        m_growth_rate(i, j)      = 0.0;
        m_healing_rate(i, j)     = 0.0;
        m_flow_enhancement(i, j) = 1.0;
        m_toughness(i, j)        = 0.0;
      }
    }

    if (constant_fd) { // no fd evolution
      D_new(i, j) = D(i, j);
    }
  }

  A.copy_from(A_new);
  D.copy_from(D_new);
}

DiagnosticList FractureDensity::diagnostics_impl() const {
  return {{"fracture_density", Diagnostic::wrap(m_density)},
          {"fracture_growth_rate", Diagnostic::wrap(m_growth_rate)},
          {"fracture_healing_rate", Diagnostic::wrap(m_healing_rate)},
          {"fracture_flow_enhancement", Diagnostic::wrap(m_flow_enhancement)},
          {"fracture_age", Diagnostic::wrap(m_age)},
          {"fracture_toughness", Diagnostic::wrap(m_toughness)}
  };
}

const array::Scalar1& FractureDensity::density() const {
  return m_density;
}

const array::Scalar& FractureDensity::growth_rate() const {
  return m_growth_rate;
}

const array::Scalar& FractureDensity::healing_rate() const {
  return m_healing_rate;
}

const array::Scalar& FractureDensity::flow_enhancement() const {
  return m_flow_enhancement;
}

const array::Scalar& FractureDensity::age() const {
  return m_age;
}

const array::Scalar& FractureDensity::toughness() const {
  return m_toughness;
}

} // end of namespace pism
