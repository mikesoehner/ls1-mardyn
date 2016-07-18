#!/usr/bin/python

# This script tests that no changes to simulation output were introduced between two MarDyn binaries
# E.g. we change the structure of the cache-s to SoA-s and we want that the program gives the same
# physical output as before.
#
# I'm starting a first basic version, many options will be added as this script is turned into a
# more general tool which Jenkins automatically tests.
#
# For the moment, this will test the output of a ResultWriter MarDyn output-plugin.
#
# Author: Nikola Tchipev
# 8. Mai 2016


from getopt import getopt
from sys import argv
from subprocess import call
from glob import glob
from ntpath import basename
import os
import ntpath
import cmd
from subprocess import Popen, PIPE
from shlex import split
import compareHelpers
#from twisted.internet.defer import returnValue

mpi = '-1'
newMarDyn = ''
oldMarDyn = '-1'
cfgFilename = ''
inpFilename = ''
comparePlugins = ['ResultWriter', 'GammaWriter', 'RDF']
numIterations = '25'


options, remainder = getopt(argv[1:], 'm:n:o:c:i:p:I:h', 
                            ['mpi=',
                             'newMarDyn=',
                             'oldMarDyn=',
                             'cfgFilename=',
                             'inpFilename=',
                             'plugin=',
                             'numIterations=',
                             'help'
                             ])
nonDefaultPlugins = False

for opt, arg in options:
    if opt in ('-n', '--newMarDyn'):
        newMarDyn = arg
    elif opt in ('-o', '--oldMarDyn'):
        oldMarDyn = arg
    elif opt in ('-c', '--cfgFilename'):
        cfgFilename = arg
    elif opt in ('-i', '--inpFilename'):
        inpFilename = arg
    elif opt in ('-m', '--mpi'):
        mpi = arg
    elif opt in ('-p', '--plugin'):
        if(not nonDefaultPlugins):  # first encounter of "-p" -> clear plugin list
            nonDefaultPlugins = True
            comparePlugins = []
        comparePlugins.append(arg)
    elif opt in ('-I', '--numIterations'):
        numIterations = arg
    elif opt in ('-h', '--help'):
        print "Make sure two versions of mardyn produce identical simulation results. Sample usage:"
        print """ ./vr -m 4 -n MarDyn.PAR_RELEASE_AVX2 -o MarDyn.PAR_RELEASE_AOS -c ../examples/surface-tension_LRC/C6H12_500/C6H12_500_1R.cfg -i ../examples/surface-tension_LRC/C6H12_500/C6H12_500.inp -p GammaWriter -I 10 """
        print """ multiple -p are possible. Currently ResultWriter, GammaWriter and RDF are supported."""
        exit(1)
    else:
        print "unknown option: " + opt
        exit(1)

SEQ = (mpi == '-1')
PAR = not SEQ   
        
noReferenceRun = (oldMarDyn == '-1')
doReferenceRun = not noReferenceRun

MPI_START='mpirun' # e.g. I need to set it to mpirun.mpich locally

comparePostfixes = []
for comparePlugin in comparePlugins:
    if comparePlugin == 'ResultWriter':
        comparePostfixes.append('.res')
    elif comparePlugin == 'GammaWriter':
        comparePostfixes.append('.gamma')
    elif comparePlugin == 'RDF':
        comparePostfixes.append('.rdf')
    else:
        print "Plugin " + comparePlugin + " not supported yet."
        print "Have a look whether you can add it yourself."
        exit(1)


if noReferenceRun:
    print "no old version given. Will try to reuse existing output, by not erasing it at start."

# JUMP to validationRuns - extract path to validationRuns from argv[0]!
pathToValidationRuns = ntpath.dirname(os.path.realpath(__file__))
pathToValidationRuns = os.path.realpath(pathToValidationRuns)

print pathToValidationRuns

# first clean all the folders
cleanUpCommand = ['rm']
cleanUpCommand.extend(glob(pathToValidationRuns + '/*.cfg'))
cleanUpCommand.extend(glob(pathToValidationRuns + '/*.inp'))
cleanUpCommand.extend(glob(pathToValidationRuns + '/new/*'))
if doReferenceRun:
    cleanUpCommand.extend(glob(pathToValidationRuns + '/reference/*'))
cleanUpCommand.extend(glob(pathToValidationRuns + '/MarDyn*'))
call(cleanUpCommand)

# copy all there
call(['cp', newMarDyn, cfgFilename, inpFilename, pathToValidationRuns])
if doReferenceRun:
    call(['cp', oldMarDyn, pathToValidationRuns])

# go there
os.chdir(pathToValidationRuns)

# get the basenames
cfgBase = ntpath.basename(cfgFilename)
inpBase = ntpath.basename(inpFilename)
oldMarDynBase = ntpath.basename(oldMarDyn)
newMarDynBase = ntpath.basename(newMarDyn)

#print "append ComparisonWriter here"
with open(cfgBase, "a") as myfile:
    for comparePlugin in comparePlugins:
        if comparePlugin == 'RDF':  # somehow configuring RDF within the cfg is different...
            myfile.write("\
             initStatistics 0\n\
             RDF 0.003 1000\n\
             RDFOutputTimesteps 10\n\
             RDFOutputPrefix val.comparison\n")
        else:
            myfile.write("output " + comparePlugin + " 1 val.comparison\n")

comparisonFilenames = []
for comparePostfix in comparePostfixes:
    comparisonFilenames.append('val.comparison' + comparePostfix)

if doReferenceRun:
    call(['cp', cfgBase, 'reference/'])
    call(['cp', inpBase, 'reference/'])
    call(['cp', oldMarDynBase, 'reference/'])
call(['cp', cfgBase, 'new/'])
call(['cp', inpBase, 'new/'])
call(['cp', newMarDynBase, 'new/'])

def doRun(directory, MardynExe, remoteLocation=""):
    # first run
    os.chdir(directory)
    cmd = []
    if PAR:
        cmd.extend([MPI_START, '-n', str(mpi)])
    cmd.extend(['./' + MardynExe, "--final-checkpoint=0", cfgBase, numIterations]); 
    print cmd
    p = Popen(cmd, stdout=PIPE, stderr=PIPE)
    out, err = p.communicate()
    #print out, err
    
    if "RDF" in comparePlugins:
        p = Popen(['ls', '-t'] + glob("val.comparison*.rdf"), stdout=PIPE, stderr=PIPE)
        out, err = p.communicate()
        p = Popen(split("mv "+ split(out)[0] + " val.comparison.rdf")) 
        # Copy newest rdf file to val.comparison.rdf
        p.wait()
    for comparisonFilename in  comparisonFilenames:
        # possible switch/if statements if other comparison plugins require different output.
        p = Popen(split("sed -i.bak /[Mm]ar[Dd]yn/d " + comparisonFilename))  # deletes lines containing MarDyn, mardyn, Mardyn or marDyn. 
        # These are the lines containing timestamps, and have to be removed for proper comparison.
        p.wait()
    os.chdir('..')


# first run
doRun('new', newMarDynBase)

## second run
if doReferenceRun:
    doRun('reference', oldMarDynBase)
returnValue = 0
#call(['diff' 'reference/val.comparison.res' 'new/val.comparison.res'])
print ""
for i in range(len(comparePlugins)):
    localReturn = compareHelpers.compareFiles("reference/" + comparisonFilenames[i], "new/" + comparisonFilenames[i])
    returnValue += localReturn
    if localReturn == 0:
        print "Identical values! for ", comparePlugins[i]
    else:
        print "mismatches for ", comparePlugins[i]


if returnValue == 0:
    print ""
    print "Identical values!"
    print ""
    exit(0)
else:
    print ""
    print "mismatches"
    print ""
    exit(1)
