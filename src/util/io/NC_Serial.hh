// Copyright (C) 2012, 2013, 2014, 2015, 2017, 2019, 2020, 2023, 2024 PISM Authors
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#ifndef PISM_NC_SERIAL_H
#define PISM_NC_SERIAL_H

#include "pism/util/io/NCFile.hh"

namespace pism {
namespace io {

class NC_Serial : public NCFile
{
public:
  NC_Serial(MPI_Comm com);
  virtual ~NC_Serial();

  std::string get_format() const;

protected:
  // implementations:
  // open/create/close
  void open_impl(const std::string &filename, io::Mode mode);

  virtual void create_impl(const std::string &filename);

  void sync_impl() const;

  void close_impl();

  // redef/enddef
  void enddef_impl() const;

  void redef_impl() const;

  // dim
  void def_dim_impl(const std::string &name, size_t length) const;

  void inq_dimid_impl(const std::string &dimension_name, bool &exists) const;

  void inq_dimlen_impl(const std::string &dimension_name, unsigned int &result) const;

  void inq_unlimdim_impl(std::string &result) const;

  // var
  virtual void def_var_impl(const std::string &name, io::Type nctype, const std::vector<std::string> &dims) const;

  void get_vara_double_impl(const std::string &variable_name,
                      const std::vector<unsigned int> &start,
                      const std::vector<unsigned int> &count,
                      double *ip) const;

  void put_vara_double_impl(const std::string &variable_name,
                      const std::vector<unsigned int> &start,
                      const std::vector<unsigned int> &count,
                      const double *op) const;

  void inq_nvars_impl(int &result) const;

  void inq_vardimid_impl(const std::string &variable_name, std::vector<std::string> &result) const;

  void inq_varnatts_impl(const std::string &variable_name, int &result) const;

  void inq_varid_impl(const std::string &variable_name, bool &exists) const;

  void inq_varname_impl(unsigned int j, std::string &result) const;

  virtual void set_compression_level_impl(int level) const;

  // att
  void get_att_double_impl(const std::string &variable_name, const std::string &att_name, std::vector<double> &result) const;

  void get_att_text_impl(const std::string &variable_name, const std::string &att_name, std::string &result) const;

  void put_att_double_impl(const std::string &variable_name, const std::string &att_name, io::Type xtype, const std::vector<double> &data) const;

  void put_att_text_impl(const std::string &variable_name, const std::string &att_name, const std::string &value) const;

  void inq_attname_impl(const std::string &variable_name, unsigned int n, std::string &result) const;

  void inq_atttype_impl(const std::string &variable_name, const std::string &att_name, io::Type &result) const;

  // misc
  void set_fill_impl(int fillmode, int &old_modep) const;

  void del_att_impl(const std::string &variable_name, const std::string &att_name) const;

  int m_rank;

  int get_varid(const std::string &variable_name) const;

private:
  void get_var_double(const std::string &variable_name, const std::vector<unsigned int> &start,
                      const std::vector<unsigned int> &count, double *ip) const;
};

} // end of namespace io
} // end of namespace pism

#endif /* PISM_NC_SERIAL_H */
