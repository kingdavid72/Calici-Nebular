#include <iostream>
#include <array>
#include <vector>
#include <cmath>
#include <limits>
#include <sstream>
#include <openbabel/obconversion.h>
#include <openbabel/mol.h>
using namespace std;
using namespace OpenBabel;

double dist2(const array<double, 3>& p0, const array<double, 3>& p1)
{
	const auto d0 = p0[0] - p1[0];
	const auto d1 = p0[1] - p1[1];
	const auto d2 = p0[2] - p1[2];
	return d0 * d0 + d1 * d1 + d2 * d2;
}

int main(int argc, char* argv[])
{
	const size_t num_references = 4;
	const size_t num_subsets = 5;
	const array<string, num_subsets> SmartsPatterns =
	{
		"[!#1]", // heavy
		"[#6+0!$(*~[#7,#8,F]),SH0+0v2,s+0,S^3,Cl+0,Br+0,I+0]", // hydrophobic
		"[a]", // aromatic
		"[$([O,S;H1;v2]-[!$(*=[O,N,P,S])]),$([O,S;H0;v2]),$([O,S;-]),$([N&v3;H1,H2]-[!$(*=[O,N,P,S])]),$([N;v3;H0]),$([n,o,s;+0]),F]", // acceptor
		"[N!H0v3,N!H0+v4,OH+0,SH+0,nH+0]", // donor
	};
	OBConversion obConversion;
	obConversion.SetInFormat("pdbqt");
	while (true)
	{
		// Parse PDBQT into atoms, and save a copy in a stringstream to pipe to OBConversion.
		vector<array<double, 3>> atoms;
		atoms.reserve(80);
		stringstream ss;
		for (string line; getline(cin, line);)
		{
			ss << line << endl;
			const auto record = line.substr(0, 6);
			if (record == "TORSDO") break; // Change to "ENDMDL" if the input PDBQT is from idock output.
			if (record != "ATOM  " && record != "HETATM") continue;
			atoms.push_back({ stod(line.substr(30, 8)), stod(line.substr(38, 8)), stod(line.substr(46, 8)) });
		}
		const size_t num_atoms = atoms.size();
		if (!num_atoms) break;

		// Call OpenBabel API to categorize atoms into pharmacophoric subsets.
		OBMol obMol;
		obConversion.Read(&obMol, &ss);
		array<vector<int>, num_subsets> subsets;
		for (size_t k = 0; k < num_subsets; ++k)
		{
			auto& subset = subsets[k];
			subset.reserve(num_atoms);
			OBSmartsPattern smarts;
			smarts.Init(SmartsPatterns[k]);
			smarts.Match(obMol);
			for (const auto& map : smarts.GetMapList())
			{
				subset.push_back(map.front() - 1);
			}
		}

		// Determine the reference points.
		const auto& subset0 = subsets.front();
		const auto n = subset0.size();
		const auto v = 1.0 / n;
		array<array<double, 3>, num_references> references{};
		auto& ctd = references[0];
		auto& cst = references[1];
		auto& fct = references[2];
		auto& ftf = references[3];
		for (size_t k = 0; k < 3; ++k)
		{
			for (const auto i : subset0)
			{
				const auto& a = atoms[i];
				ctd[k] += a[k];
			}
			ctd[k] *= v;
		}
		double cst_dist = numeric_limits<double>::max();
		double fct_dist = numeric_limits<double>::lowest();
		double ftf_dist = numeric_limits<double>::lowest();
		for (const auto i : subset0)
		{
			const auto& a = atoms[i];
			const auto this_dist = dist2(a, ctd);
			if (this_dist < cst_dist)
			{
				cst = a;
				cst_dist = this_dist;
			}
			if (this_dist > fct_dist)
			{
				fct = a;
				fct_dist = this_dist;
			}
		}
		for (const auto i : subset0)
		{
			const auto& a = atoms[i];
			const auto this_dist = dist2(a, fct);
			if (this_dist > ftf_dist)
			{
				ftf = a;
				ftf_dist = this_dist;
			}
		}

		// Precalculate the distances of heavy atoms to the reference points, given that subsets[1 to 4] are subsets of subsets[0].
		array<vector<double>, num_references> dista;
		for (size_t k = 0; k < num_references; ++k)
		{
			const auto& reference = references[k];
			auto& dists = dista[k];
			dists.resize(num_atoms);
			for (size_t i = 0; i < n; ++i)
			{
				dists[subset0[i]] = sqrt(dist2(atoms[subset0[i]], reference));
			}
		}

		// Loop over pharmacophoric subsets and reference points.
		for (const auto& subset : subsets)
		{
			const auto n = subset.size();
			for (size_t k = 0; k < num_references; ++k)
			{
				// Load distances from precalculated ones.
				const auto& distp = dista[k];
				vector<double> dists(n);
				for (size_t i = 0; i < n; ++i)
				{
					dists[i] = distp[subset[i]];
				}

				// Compute moments.
				array<double, 3> m{};
				if (n > 2)
				{
					const auto v = 1.0 / n;
					for (size_t i = 0; i < n; ++i)
					{
						const auto d = dists[i];
						m[0] += d;
					}
					m[0] *= v;
					for (size_t i = 0; i < n; ++i)
					{
						const auto d = dists[i] - m[0];
						m[1] += d * d;
					}
					m[1] = sqrt(m[1] * v);
					for (size_t i = 0; i < n; ++i)
					{
						const auto d = dists[i] - m[0];
						m[2] += d * d * d;
					}
					m[2] = cbrt(m[2] * v);
				}
				else if (n == 2)
				{
					m[0] = 0.5 *     (dists[0] + dists[1]);
					m[1] = 0.5 * fabs(dists[0] - dists[1]);
				}
				else if (n == 1)
				{
					m[0] = dists[0];
				}

				// Write moments.
				cout.write(reinterpret_cast<char*>(m.data()), sizeof(m));
			}
		}
	}
}
