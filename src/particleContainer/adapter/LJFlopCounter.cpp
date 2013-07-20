/**
 * \file
 * \brief A CellProcessor that produces Flop information.
 * \author Johannes Heckl
 */

#include "LJFlopCounter.h"

#include "particleContainer/ParticleCell.h"
#include "molecules/Molecule.h"
#include "parallel/DomainDecompBase.h"
#include "Simulation.h"
#include "utils/Logger.h"

LJFlopCounter::LJFlopCounter(double rc) : _rc2(rc * rc) {
	_totalCounts.clear();
}

void LJFlopCounter::initTraversal(const size_t numCells) {
	_currentCounts.clear();
}

void LJFlopCounter::endTraversal() {

	DomainDecompBase& domainDecomp =  global_simulation->domainDecomposition();
	domainDecomp.collCommInit(3);
	domainDecomp.collCommAppendDouble(_currentCounts.calc_molDist);
	domainDecomp.collCommAppendDouble(_currentCounts.calc_LJ);
	domainDecomp.collCommAppendDouble(_currentCounts.calc_Macro);
	domainDecomp.collCommAllreduceSum();
	_currentCounts.calc_molDist = domainDecomp.collCommGetDouble();
	_currentCounts.calc_LJ = domainDecomp.collCommGetDouble();
	_currentCounts.calc_Macro = domainDecomp.collCommGetDouble();
	domainDecomp.collCommFinalize();

	_totalCounts.addCounts(_currentCounts);

	const double cflMolDist = _currentCounts.calc_molDist * _flops_MolDist;
	const double cflCenterDist = _currentCounts.calc_LJ * _flops_CenterDist;
	const double cflLJKernel = _currentCounts.calc_LJ * _flops_LJKernel;
	const double cflLJSum = _currentCounts.calc_LJ * _flops_LJSum;
	const double cflMacro = _currentCounts.calc_Macro * _flops_MacroValues;
	const double cflMacroSum = _currentCounts.calc_Macro * _flops_MacroSum;
	const double cflTotal = cflMolDist + cflCenterDist + cflLJKernel + cflLJSum + cflMacro + cflMacroSum;

	Log::global_log->info()
			<< "FLOP counts in LJ force calculation for this iteration:"
			<< std::endl << " Molecule distance: " << cflMolDist
			<< " Center distance: " << cflCenterDist << " LJ Kernel: "
			<< cflLJKernel << " LJ Sum: " << cflLJSum << " Macroscopic values: "
			<< cflMacro << " Macroscopic value sum: " << cflMacroSum << std::endl
			<< "Current total FLOPS: " << cflTotal << std::endl;


	const double flMolDist = _totalCounts.calc_molDist * _flops_MolDist;
	const double flCenterDist = _totalCounts.calc_LJ * _flops_CenterDist;
	const double flLJKernel = _totalCounts.calc_LJ * _flops_LJKernel;
	const double flLJSum = _totalCounts.calc_LJ * _flops_LJSum;
	const double flMacro = _totalCounts.calc_Macro * _flops_MacroValues;
	const double flMacroSum = _totalCounts.calc_Macro * _flops_MacroSum;
	_totalFlopCount = flMolDist + flCenterDist + flLJKernel + flLJSum + flMacro + flMacroSum;

	Log::global_log->info() << "Accumulated FLOP counts in LJ force calculation:"
			<< std::endl << " Molecule distance: " << flMolDist
			<< " Center distance: " << flCenterDist << " LJ Kernel: "
			<< flLJKernel << " LJ Sum: " << flLJSum << " Macroscopic values: "
			<< flMacro << " Macroscopic value sum: " << flMacroSum << std::endl
			<< "Accumulated total FLOPS: " << _totalFlopCount << std::endl;
}

void LJFlopCounter::preprocessCell(ParticleCell & c) {
}

void LJFlopCounter::postprocessCell(ParticleCell & c) {
}

void LJFlopCounter::processCell(ParticleCell & c) {
	const MoleculeList & molecules = c.getParticlePointers();
	if (molecules.size() > 1) {
		const MoleculeList::const_iterator end_i = --(molecules.end());
		const MoleculeList::const_iterator end_j = molecules.end();

		for (MoleculeList::const_iterator i = molecules.begin(); i != end_i;
				++i) {
			MoleculeList::const_iterator j = i;
			++j;
			for (; j != end_j; ++j) {

				// Have to compare the distance between 2 molecules.
				_currentCounts.calc_molDist += 1;

				const double d_x = (*i)->r(0) - (*j)->r(0);
				const double d_y = (*i)->r(1) - (*j)->r(1);
				const double d_z = (*i)->r(2) - (*j)->r(2);
				const double d2 = d_x * d_x + d_y * d_y + d_z * d_z;
				if (d2 < _rc2) {
					const size_t centers_i = (*i)->numLJcenters();
					const size_t centers_j = (*j)->numLJcenters();

					// Have to calculate the LJ force for each pair of centers.
					_currentCounts.calc_LJ += centers_i * centers_j;
					// Have to calculate macroscopic values for each pair of centers.
					_currentCounts.calc_Macro += centers_i * centers_j;
				}
			}
		}
	}
}

void LJFlopCounter::processCellPair(ParticleCell & c1, ParticleCell & c2) {
	const MoleculeList & molecules1 = c1.getParticlePointers();
	const MoleculeList & molecules2 = c2.getParticlePointers();
	if ((molecules1.size() > 0) && (molecules2.size() > 0)) {
		const MoleculeList::const_iterator end_i = molecules1.end();
		const MoleculeList::const_iterator end_j = molecules2.end();

		for (MoleculeList::const_iterator i = molecules1.begin(); i != end_i;
				++i) {
			for (MoleculeList::const_iterator j = molecules2.begin();
					j != end_j; ++j) {

				// Have to compare the distance between 2 molecules.
				_currentCounts.calc_molDist += 1;

				const double d_x = (*i)->r(0) - (*j)->r(0);
				const double d_y = (*i)->r(1) - (*j)->r(1);
				const double d_z = (*i)->r(2) - (*j)->r(2);
				const double d2 = d_x * d_x + d_y * d_y + d_z * d_z;
				if (d2 < _rc2) {
					const size_t centers_i = (*i)->numLJcenters();
					const size_t centers_j = (*j)->numLJcenters();

					// Have to calculate the LJ force for each pair of centers.
					_currentCounts.calc_LJ += centers_i * centers_j;

					if ((c1.isHaloCell() == (!c2.isHaloCell()))
							&& ((*i)->isLessThan(**j))) {
						// Have to calculate macroscopic values for each pair of centers.
						_currentCounts.calc_Macro += centers_i * centers_j;
					}
				}
			}
		}
	}
}