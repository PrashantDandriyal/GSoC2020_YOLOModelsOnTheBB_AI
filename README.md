### GSoC2020 | YOLO v2 | BeagleBone AI 

Open-source material in requirement of Google Summer of Code 2020

* The BeagleBone AI is based on the Texas Instruments Sitara Processor AM5729.

* The model used is YOLO v2-tiny (COCO dataset). It consists of 9 Convolutional layers and 6 MaxPooling layers. TIDL [recommends](http://downloads.ti.com/mctools/esd/docs/tidl-api/using_api.html#frame-split-across-eos) 
> Certain network layers such as Softmax and **Pooling run faster on the C66x** vs. EVE. Running these layers on C66x can lower the per-frame latency.

Hence, if we assign Convolution layers (to run on _EVE_) are placed in a Layer Group (say 1) and run on EVE. Pooling and SoftMax layers are placed in a second Layer Group(say 2) and run on _C66x_. The EVE layer group takes ~57.5ms, C66x layer group takes ~6.5ms. So total exectution time = 64ms. 

_**Note:**_ The results are quoted from the tests conducted to process a single frame 224x224x3 on **AM574x** [IDK EVM](https://www.ti.com/tool/TMDSIDK574) with JacintoNet11 imagenet model, TIDL API v1.1. 
The Kit includes only 2 EVEs + 2 DSPs. The AM5729 on the other side has 4 EVEs + 2 DSPs.




