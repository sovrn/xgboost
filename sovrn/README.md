# XGBoost build commands

## Environment Setup

For ease of use, we set up a directory structure for the Docker environment to build in that is external
to the Docker container. This allows us to build the XGBoost library and copy the resulting JAR file.

## Cross-Compile XGBoost for ARM64

```bash
git clone --recursive git@github.com:sovrn/xgboost
cd xgboost
git checkout v1.7.5-sovrn-v1.1 #this tag will change and should be updated with subsequent updates
```

_Alternatively: `git checkout -b branchname v1.7.5-sovrn-v1.1` if you want a new branch based on the label._

```bash
cd sovrn
docker build --platform linux/arm64 -t xgboost-build .
docker run -v $(pwd)/..:/xgboost --rm -it xgboost-build
```

Now you should have a Docker ready to build your jar for aarch64/ARM64.

# Inside the Docker

```bash
mkdir build
cd build
cmake .. -DUSE_CUDA=OFF -DJVM_BINDINGS=ON -DUSE_NCCL=OFF -DBUILD_WITH_CUDA_CUB=OFF -DUSE_OPENMP=ON
make -j$(nproc)
cd ../jvm-packages/
mvn clean install
```

_You may see xgboost4j-flink_2.12 fail to build, we aren't using that package, so it doesn't matter._

Next, we build the jar file.
```bash
mvn package
ls xgboost4j/target/
```

You should see `xgboost4j_2.12-1.7.5-sovrn-dmatrix-fix.jar` here. This is the jar file you need to push to Artifactory
or copy to Blackbird.

Be sure to `exit` the Docker at this point. The jar will exist outside the Docker.

To upload to Artifactory, follow these instructions: [Upload to Artifactory](https://authcore.atlassian.net/wiki/spaces/EX/pages/2693302714392/Building+XGBoost#Uploading-jar-to-Artifactory)

## Updating Tags if Making Changes

If you end up changing the build, work off the `1.7.5-sovrn-dmatrix-fix` branch.
When you're ready to update the tag, do the following, and rotate the `sovrn-v1.x` tag as needed:

```bash
git tag v1.7.5-sovrn-v1.x
git push origin v1.7.5-sovrn-v1.x
```

Push your change to the branch as usual.

**NOTE:** We should automate this process, if we keep living in this way.
