# entanglement_dev_mapper_target

This is a research project done at the DEDIS lab at EPFL, as part of a semester research project, under the supervision of Dr. Vero Estrada-Galinanes. 

This project represents a device mapper target which implements a redundancy scheme on top of any block device. The redundnacy scheme is called Simple Entanglements, based on the following paper: 
https://ieeexplore.ieee.org/abstract/document/7820651

The motivation behind this project was to create a redundnacy mechanism which can be used in Shufflecake, a coercion-resistant tool for creating multiple hidden volumes on a device. 
This module should be usable on top of any block device, in particular for Shufflecake, on top of each volume separately. 

