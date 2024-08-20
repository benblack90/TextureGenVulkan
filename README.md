# MSc Computer Game Engineering Dissertation - Procedurally Generating Gas Giant Textures
This program uses Vulkan to procedurally generate gas giant textures that evolve in appearance over time. It uses domain warping to achieve this look. 
The main files containing the functionality to do this are:
VulkanTutorials\GasGiantTexGen.h
VulkanTutorials\GasGiantTexGen.cpp
VulkanTutorials\GasGiantTex.comp
VulkanTutorials\BasicCompute.vert
VulkanTutorials\BasicCompute.frag

The first three files contain code wholly written by me (Ben Schwarz). The latter two are trivially short shaders designed to throw vertices on screen with little manipulation
mostly written by Rich Davison, but with small additions by me. 

## Building the project

Open CL tool of choice, then

```
mk dir build
cd build
cmake ..
```

## Running the project and controls

The project is best run out of Visual Studio. Once open:

Escape - end program
Right arrow - add one texture to the compute queue, and view the newest one
Left arrow - go back one texture, and remove the previous one from the compute queue
minus/dash key (not numpad) - decrease quality via octave settings. There are three possible settings, and the program starts set to the highest
plus key (not numpad) - increase quality via octave settings
t - enable/disable timed mode. All other controls (bar escape) are disbled. The program will collect timestamp data on 1000 frames, before increasing the number of textures. 
    In the console, results appear like so:
    1,0.23122,0.0141341
    The data means the following:
    num planet textures | avg time from top of pipe to end of compute pipe | avg time from end of compute pipe to end of render pipe