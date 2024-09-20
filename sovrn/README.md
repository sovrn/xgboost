# XGBoost build commands

```bash
git clone --recursive git@github.com:sovrn/xgboost

docker build --platform linux/arm64 -t myapp-build .
docker run -v $(pwd):/xgboost --rm -it myapp-build

mkdir build
cd build
cmake .. -DUSE_CUDA=OFF -DJVM_BINDINGS=ON -DUSE_NCCL=OFF -DBUILD_WITH_CUDA_CUB=OFF -DUSE_OPENMP=ON
make -j$(nproc)
cd ../jvm-packages/
mvn clean install
mvn package
ls xgboost4j/target/


xgboost build copy:
cp ../xgboost-build-docker/xgboost/jvm-packages/xgboost4j/target/xgboost4j_2.12-2.0.0-SNAPSHOT.jar ./blackbird/build/docker/libs/thirdparty/xgboost4j_2.12-1.7.5-sovrn-arm.jar 
```
