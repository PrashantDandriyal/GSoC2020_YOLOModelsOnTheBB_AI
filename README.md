### GSoC2020 | YOLO v2 | BeagleBone AI 

Open-source material in requirement of Google Summer of Code 2020

* The BeagleBone AI is based on the Texas Instruments Sitara Processor AM5729.

* The model used is YOLO v2-tiny (COCO dataset). It consists of 9 Convolutional layers and 6 MaxPooling layers. The model is converted to TensorFlow frozen graph (`.pb` file) using the method explained in another [repository](). 

* The souce codes are added to the repository for understanding the 2 approaches using the programming model.

* TIDL [recommends](http://downloads.ti.com/mctools/esd/docs/tidl-api/using_api.html#frame-split-across-eos) 
> Certain network layers such as Softmax and **Pooling run faster on the C66x** vs. EVE. Running these layers on C66x can lower the per-frame latency.

 Hence, for demonstration purpose, if Convolution layers (to run on _EVE_) are placed in a Layer Group (say 1) and run on EVE. Pooling and SoftMax layers are placed in a second Layer Group(say 2) and run on _C66x_. The EVE layer group takes ~57.5ms, C66x layer group takes ~6.5ms. So total exectution time = 64ms. The frames are processed **one at a time**. The approximate API overhead is 1.6% of the total device processing time _[Source](http://downloads.ti.com/mctools/esd/docs/tidl-api/example.html#imagenet)_. 

_**Note:**_ The results are quoted from the tests conducted to process a single frame 224x224x3 on **AM574x** [IDK EVM](https://www.ti.com/tool/TMDSIDK574) with JacintoNet11 imagenet model, TIDL API v1.1. 
The Kit includes only 2 EVEs + 2 DSPs. The AM5729 on the other side has 4 EVEs + 2 DSPs.

* The JacintoNet11 v2 model (see first figure below) has 14 _equivalent_ layers and the YOLO v2-tiny (second figure below) has 17 equivalent layers. This is when we heuristically draw the equivalent model. In this, the _batchNormalization_ layer is considered as a separate layer and not coalesced into a single layer like _Convolution+Relu_. The actual equivalent model can only be obtained using the [Model Visualizer Tool]() in TIDL. It is likely that the _batchNormalization_ layer is coalesced and the number of layers drops to 10.

![JacintoNet11v2](https://github.com/PrashantDandriyal/GSoC2020_YOLOModelsOnTheBB_AI/blob/master/Jacinto11v2.png) and ![yolov2tiny](https://github.com/PrashantDandriyal/GSoC2020_YOLOModelsOnTheBB_AI/blob/master/yolov2Tiny_arch.png)

* Unlike the above quoted demo case, layer grouping for our model shall differ. We create layer groups in 2 ways. The approach giving better performance is chosen. The determining factor is the frame execution time of any _EVE_ (`exec_time_eve`)and a _DSP_(`exec_time_dsp`).

```
if(exec_time_eve == exec_time_dsp):
	use Approach_1
else
	use Approach_2
```

 **1) Approach 1: One _Execution Object_ (EO) per frame with (Only EVEs)** 

 Process 1 frame per _EO_ or 1 per _EOP_ (4 EVEs and 2 DSPs). This means 6 frames per EO. Above mentioned demo uses 2 EVEs + 2 DSPs (4 _EOs_) but not for distibuting frames but for layer grouping. Hence, the overall effect is that of a single frame at a time. This method doesn't leverage the layer grouping. The expected performance is 6x (10ms+2ms API overhead). The method is memory intensive beacause each _EO_ is alloted input and output buffer individually. The source code is developed assuming pre-processed input data is available. In all other cases, OpenCV tools are readily available to do so.

Source Code: [aproach_1](https://github.com/PrashantDandriyal/GSoC2020_YOLOModelsOnTheBB_AI/blob/master/approach_1.cpp)

Network heap size : `64MB/EO x 6 EO = 384MB`

  **2) Approach 2: Two _EO_ per frame using Double Buffering (EVEs+DSPs)**

The second approach is similar to the one adopted in the demo, but the DSPs are replaced with additional EVEs. The pipelining used in the demo can be used to understand this approach also.

![Demo on AM5749](http://downloads.ti.com/mctools/esd/docs/tidl-api/_images/tidl-frame-across-eos-opt.png)

The TIDL device translation tool assigns layer group ids to layers during the translation process. But if the assignment fails to distribute the layers evenly, we use explicit grouping using the configuration file or the main cpp file. In this, for each frame, the first few layers (preferrably half of them) are grouped to be executed on EVE0 and the remaining half are grouped to run on EVE1. Similarly for the other frame on EVE3 and EVE 4. There are 4 _EOs_ (4 EVEs and 0 DSPs) and 2 _EOPs_ (Each _EOP_ contains a pair of EVE). We process 1 frame per _EOP_, so 2 frames at a time. A good performance is expected due to the  distribution of overload between the EVEs and use  [_double buffering_](http://downloads.ti.com/mctools/esd/docs/tidl-api/using_api.html#using-eops-for-double-buffering). 

Source Code: [aproach_2](https://github.com/PrashantDandriyal/GSoC2020_YOLOModelsOnTheBB_AI/blob/master/approach_2.cpp)


 


