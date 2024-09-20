# Use Amazon Corretto 8 JDK based on Amazon Linux 2023
FROM amazoncorretto:8-al2023-jdk

# Update and install necessary tools
RUN yum update -y && \
    yum install -y gcc-c++ make git maven python3-devel cmake wget tar

# Install Scala
RUN wget https://downloads.lightbend.com/scala/2.12.17/scala-2.12.17.tgz -P /tmp && \
    tar xzf /tmp/scala-2.12.17.tgz -C /usr/local && \
    ln -s /usr/local/scala-2.12.17 /usr/local/scala && \
    rm /tmp/scala-2.12.17.tgz

# Set environment variables for Java, Python, Maven, and Scala
ENV JAVA_HOME=/usr/lib/jvm/java-1.8.0-amazon-corretto.aarch64
ENV PATH=${PATH}:${JAVA_HOME}/bin:/usr/local/scala/bin

# Set Java 8 as the default Java version
RUN alternatives --set java ${JAVA_HOME}/jre/bin/java

# Create a symlink for Python
RUN ln -s /usr/bin/python3 /usr/bin/python

# Set the working directory to /xgboost
WORKDIR /xgboost
