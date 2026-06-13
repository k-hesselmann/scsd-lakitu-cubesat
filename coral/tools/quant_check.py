import tensorflow as tf
i = tf.lite.Interpreter(model_path="/home/khess/ml-payload/cloud_regressor_qat_int8.tflite")
i.allocate_tensors()
inp = i.get_input_details()[0]; out = i.get_output_details()[0]
print("in ", inp["dtype"], inp["shape"], inp["quantization"])
print("out", out["dtype"], out["shape"], out["quantization"])
