//
// Created by Kruegener on 8/27/2018.
// Ported from Domain.cpp by Mheier.
//

#include "Velocity3dProfile.h"
#include "DensityProfile.h"

void Velocity3dProfile::output (string prefix, long unsigned accumulatedDatasets) {
	global_log->info() << "[Velocity3dProfile] output" << std::endl;

	// Setup outfile
	_accumulatedDatasets = accumulatedDatasets;
	_profilePrefix = prefix;
	_profilePrefix += "_kartesian.V3Dpr";
	ofstream outfile(_profilePrefix.c_str());
	outfile.precision(6);

	// Write header
	outfile << "//Segment volume: " << _samplInfo.segmentVolume << "\n//Accumulated data sets: " << _accumulatedDatasets
			<< "\n//Local profile of X-Y-Z components of velocity. Output file generated by the \"Velocity3dProfile\" method, plugins/profiles. \n";
	outfile << "//local velocity component profile: Y - Z || X-projection\n";
	// TODO: more info
	outfile << "// \t dX \t dY \t dZ \n";
	outfile << "\t" << 1 / _samplInfo.universalInvProfileUnit[0] << "\t" << 1 / _samplInfo.universalInvProfileUnit[1]
			<< "\t" << 1 / _samplInfo.universalInvProfileUnit[2] << "\n";
	outfile << "0 \t";

	// Invoke matrix write routine
	writeMatrix(outfile);

	outfile.close();

}

void Velocity3dProfile::writeDataEntry (unsigned long uID, ofstream& outfile) const {
	long double vd;
	// X - Y - Z output
	for (unsigned d = 0; d < 3; d++) {
		// Check for division by 0
		int numberDensity = _densityProfile->getGlobalNumber(uID);
		if (numberDensity != 0) {
			vd = _global3dProfile.at(uID)[d] / numberDensity;
		} else {
			vd = 0;
		}
		outfile << vd << "\t";
	}
}
