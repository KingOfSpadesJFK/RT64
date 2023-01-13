echo "static unsigned int helloTriangleVS[] = " > src/rt64vk/shaders/shader.vert.h
echo "static unsigned int helloTriangleFS[] = " > src/rt64vk/shaders/shader.frag.h
glslc src/rt64vk/shaders/shader.vert --target-env=vulkan1.2 -fshader-stage=vert -mfmt=c -o src/rt64vk/shaders/shader.vert.h
glslc src/rt64vk/shaders/shader.frag --target-env=vulkan1.2 -fshader-stage=frag -mfmt=c -o src/rt64vk/shaders/shader.frag.h
sed -i '1s/^/static unsigned int helloTriangleVS[] = /' src/rt64vk/shaders/shader.vert.h
sed -i '1s/^/static unsigned int helloTriangleFS[] = /' src/rt64vk/shaders/shader.frag.h
echo \; >> src/rt64vk/shaders/shader.vert.h
echo \; >> src/rt64vk/shaders/shader.frag.h