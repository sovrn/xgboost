/*
 Copyright (c) 2014 by Contributors 

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at
    
 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */
package ml.dmlc.xgboost4j.java;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.Map;

import junit.framework.TestCase;
import org.junit.Test;

import java.util.Random;
import java.io.PrintStream;
import java.util.*;
import java.util.concurrent.*;

//
// Utility class for comparing array contents
//
class ArrayComparator {
  public static boolean compare(float[][] one, float[][] two) {

    // Test that both arrays are the same size in the first dimension
    if (one.length != two.length) {
      System.err.println("Array first dimensions are not identical in size");
      return false;
    }

    // Test that both arrays are the same size in the second dimension
    if (one[0].length != two[0].length) {
      System.err.println("Array second dimensions are not identical in size");
      return false;
    }

    // Test that all the array elements are identical
    for (int i=0; i<one.length; i++) {
      for (int j=0; j<one[i].length; j++) {
        if (one[i][j] != two[i][j]) {
          System.err.println("Array elements [" + i + "][" + j + "] are not identical");
          System.err.println(one[i][j] + " != " + two[i][j]);
          return false;
        }
      }
    }

    return true;
  }
}

//
// Utility class for printing out array contents
//
class ArrayPrinter {
  PrintStream stream;

  public ArrayPrinter() {
    stream = System.out;
  }
  public ArrayPrinter(PrintStream stream) {
    this.stream = stream;
  }

  public void print(String name, float[][] a) {
    stream.print(name + " = [");

    for (int i=0; i < a.length-1; i++) {
      stream.print(a[i][0] + ", ");
    }
    stream.println(a[a.length-1][0] + "]");
  }
}

//
// Performs a series of single-vector in-place predictions in a dedicated thread
//
class InplacePredictThread extends Thread {

  int thread_num;
  boolean success = true;
  float[][] testX;
  int test_rows;
  int features;
  float[][] true_predicts;
  Booster booster;
  Random rng = new Random();
  int n_preds = 100;

  public InplacePredictThread(int n, Booster booster, float[][] testX, int test_rows, int features, float[][] true_predicts) {
    this.thread_num = n;
    this.booster = booster;
    this.testX = testX;
    this.test_rows = test_rows;
    this.features = features;
    this.true_predicts = true_predicts;
  }

  public void run() {
//    System.err.println("Thread #" + thread_num + " started.");

    try {
      // Perform n_preds number of single-vector predictions
      for (int i=0; i<n_preds; i++) {
        // Randomly generate int in range 0 <= r < test_rows
        int r = this.rng.nextInt(this.test_rows);

        // In-place predict a single random row
        float[][] predictions = booster.inplace_predict(this.testX[r], 1, this.features, false);

        // Confirm results as expected
        if (predictions[0][0] != this.true_predicts[r][0]) {
//          System.err.println("Error in thread #" + this.thread_num);
          success = false;
          return;  // bail at the first error.
        }
      }
    } catch (XGBoostError e) {
      throw new RuntimeException(e);
    }
  }

  public boolean isSuccess() {
    return success;
  }
}

class InplacePredictionTask implements Callable<Boolean> {
  int task_num;
  float[][] testX;
  int test_rows;
  int features;
  float[][] true_predicts;
  Booster booster;
  Random rng = new Random();
  int n_preds = 100;

  public InplacePredictionTask(int n, Booster booster, float[][] testX, int test_rows, int features, float[][] true_predicts) {
    this.task_num = n;
    this.booster = booster;
    this.testX = testX;
    this.test_rows = test_rows;
    this.features = features;
    this.true_predicts = true_predicts;
  }

  @override
  public Boolean call() throws Exception {
    System.err.println("Task #" + task_num + " started.");

    // Perform n_preds number of single-vector predictions
    for (int i=0; i<n_preds; i++) {
      // Randomly generate int in range 0 <= r < test_rows
      int r = this.rng.nextInt(this.test_rows);

      // In-place predict a single random row
      float[][] predictions = booster.inplace_predict(this.testX[r], 1, this.features, false);

      // Confirm results as expected
      if (predictions[0][0] != this.true_predicts[r][0]) {
          System.err.println("Error in task #" + this.task_num);
        return false;  // bail at the first error.
      }
    }

    System.err.println("Task #" + task_num + " finished.");

    // No errors found
    return true;
  }
}

/**
 * test cases for Booster
 *
 * @author hzx
 */
public class BoosterImplTest {
  public static class EvalError implements IEvaluation {
    @Override
    public String getMetric() {
      return "custom_error";
    }

    @Override
    public float eval(float[][] predicts, DMatrix dmat) {
      float error = 0f;
      float[] labels;
      try {
        labels = dmat.getLabel();
      } catch (XGBoostError ex) {
        throw new RuntimeException(ex);
      }
      int nrow = predicts.length;
      for (int i = 0; i < nrow; i++) {
        if (labels[i] == 0f && predicts[i][0] > 0) {
          error++;
        } else if (labels[i] == 1f && predicts[i][0] <= 0) {
          error++;
        }
      }

      return error / labels.length;
    }
  }

  private Booster trainBooster(DMatrix trainMat, DMatrix testMat) throws XGBoostError {
    //set params
    Map<String, Object> paramMap = new HashMap<String, Object>() {
      {
        put("eta", 1.0);
        put("max_depth", 2);
        put("silent", 1);
        put("objective", "binary:logistic");
      }
    };

    //set watchList
    HashMap<String, DMatrix> watches = new HashMap<String, DMatrix>();

    watches.put("train", trainMat);
    watches.put("test", testMat);

    //set round
    int round = 5;

    //train a boost model
    return XGBoost.train(trainMat, paramMap, round, watches, null, null);
  }

  @Test
  public void testBoosterBasic() throws XGBoostError, IOException {

    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");

    Booster booster = trainBooster(trainMat, testMat);

    //predict raw output
    float[][] predicts = booster.predict(testMat, true, 0);

    //eval
    IEvaluation eval = new EvalError();
    //error must be less than 0.1
    TestCase.assertTrue(eval.eval(predicts, testMat) < 0.1f);
  }

  @Test
  public void testBoosterInplacePredict() throws  XGBoostError, IOException {

//    System.out.println("=-=-=-=-=- testBoosterInplacePredict =-=-=-=-=");

    Random rng = new Random();

    // Data generation

    // Randomly generate raining set
    int train_rows = 1000;
    int features = 10;
    int train_size = train_rows * features;
    float[] trainX = new float[train_size];
    float[] trainy = new float[train_rows];

    for (int i=0; i<train_size; i++) {
      trainX[i] = rng.nextFloat();
    }
    for (int i=0; i<train_rows; i++) {
      trainy[i] = rng.nextFloat();
    }

    DMatrix trainMat = new DMatrix(trainX, train_rows, features, Float.NaN);
    trainMat.setLabel(trainy);

//    System.out.println("Train DMatrix rows = " + trainMat.rowNum());

    // Randomly generate testing set
    int test_rows = 10;
    int test_size = test_rows * features;
    float[] testX = new float[test_size];
    float[] testy = new float[test_rows];

    for (int i=0; i<test_size; i++) {
      testX[i] = rng.nextFloat();
    }
    for (int i=0; i<test_rows; i++) {
      testy[i] = rng.nextFloat();
    }

    DMatrix testMat = new DMatrix(testX, test_rows, features, Float.NaN);
    testMat.setLabel(testy);

//    System.out.println("Test DMatrix rows = " + testMat.rowNum());

    // Training

    // Set parameters
    Map<String, Object> params = new HashMap<String, Object>() {
      {
        put("eta", 1.0);
        put("max_depth", 2);
        put("silent", 1);
        put("tree_method", "hist");
      }
    };

    Map<String, DMatrix> watches = new HashMap<String, DMatrix>() {
      {
        put("train", trainMat);
        put("test", testMat);
      }
    };

    Booster booster = XGBoost.train(trainMat, params, 10, watches, null, null);

//    System.out.println("# model features = " + booster.getNumFeature());

    // Prediction

    // standard prediction
    float[][] predicts = booster.predict(testMat);

    // inplace prediction
    float[][] inplace_predicts = booster.inplace_predict(testX, test_rows, features, false);

    // Confirm that the two prediction results are identical
    TestCase.assertTrue(ArrayComparator.compare(predicts, inplace_predicts));

    // Reformat the test matrix as 2D array
    float[][] testX2 = new float[test_rows][features];

    int k=0;
    for (int i=0; i<test_rows; i++) {
      for(int j=0; j<features; j++, k++) {
        testX2[i][j] = testX[k];
      }
    }

    // Multi-thread prediction
    int n_threads = 20;
    InplacePredictThread t[] = new InplacePredictThread[n_threads];

    // Start the prediction threads
    for (int i=0; i<n_threads; i++) {
      t[i] = new InplacePredictThread(i, booster, testX2, test_rows, features, predicts);
      t[i].start();
    }

    // Wait for each prediction thread to complete, then test for success
    for (int i=0; i<n_threads; i++) {
      try {
        t[i].join();
//        System.err.println("Thread #" +  i + " finished.");
      } catch (InterruptedException e) {
          throw new RuntimeException(e);
      }
      // Confirm that each prediction thread succeeded
      TestCase.assertTrue(t[i].isSuccess());
    }

//    System.out.println("=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-");
  }

  @Test
  public void testBoosterInplacePredict2() throws  XGBoostError, IOException {

    System.out.println("=-=-=-=-=- testBoosterInplacePredict 2 =-=-=-=-=");

    Random rng = new Random();

    // Data generation

    // Randomly generate raining set
    int train_rows = 1000;
    int features = 10;
    int train_size = train_rows * features;
    float[] trainX = new float[train_size];
    float[] trainy = new float[train_rows];

    for (int i=0; i<train_size; i++) {
      trainX[i] = rng.nextFloat();
    }
    for (int i=0; i<train_rows; i++) {
      trainy[i] = rng.nextFloat();
    }

    DMatrix trainMat = new DMatrix(trainX, train_rows, features, Float.NaN);
    trainMat.setLabel(trainy);

    // Randomly generate testing set
    int test_rows = 10;
    int test_size = test_rows * features;
    float[] testX = new float[test_size];
    float[] testy = new float[test_rows];

    for (int i=0; i<test_size; i++) {
      testX[i] = rng.nextFloat();
    }
    for (int i=0; i<test_rows; i++) {
      testy[i] = rng.nextFloat();
    }

    DMatrix testMat = new DMatrix(testX, test_rows, features, Float.NaN);
    testMat.setLabel(testy);

    // Training

    // Set parameters
    Map<String, Object> params = new HashMap<String, Object>() {
      {
        put("eta", 1.0);
        put("max_depth", 2);
        put("silent", 1);
        put("tree_method", "hist");
      }
    };

    Map<String, DMatrix> watches = new HashMap<String, DMatrix>() {
      {
        put("train", trainMat);
        put("test", testMat);
      }
    };

    Booster booster = XGBoost.train(trainMat, params, 10, watches, null, null);


    // Prediction

    // standard prediction
    float[][] predicts = booster.predict(testMat);

    // inplace prediction
    float[][] inplace_predicts = booster.inplace_predict(testX, test_rows, features, false);

    // Confirm that the two prediction results are identical
    TestCase.assertTrue(ArrayComparator.compare(predicts, inplace_predicts));

    // Reformat the test matrix as 2D array
    float[][] testX2 = new float[test_rows][features];

    int k=0;
    for (int i=0; i<test_rows; i++) {
      for(int j=0; j<features; j++, k++) {
        testX2[i][j] = testX[k];
      }
    }

    // Multi-thread prediction

    // Create thread pool
    int n_tasks = 20;
    List<Future<Boolean>> result = new ArrayList(n_tasks);
    ExecutorService executorService = Executors.newFixedThreadPool(5);  // Create pool of 5 threads

    for (int i=0; i<n_tasks; i++) {
      result.add(executorService.submit(new InplacePredictionTask(i, booster, testX2, test_rows, features, predicts)));
    }

    executorService.shutdown();

    try {
      executorService.awaitTermination(10, TimeUnit.SECONDS);

      for (int i=0; i<n_tasks; i++) {
        TestCase.assertTrue(result.get(i).get());
      }
    } catch (InterruptedException | ExecutionException e) {
        throw new RuntimeException(e);
    }

    System.out.println("=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-");
  }

  @Test
  public void saveLoadModelWithPath() throws XGBoostError, IOException {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");
    IEvaluation eval = new EvalError();

    Booster booster = trainBooster(trainMat, testMat);
    // save and load
    File temp = File.createTempFile("temp", "model");
    temp.deleteOnExit();
    booster.saveModel(temp.getAbsolutePath());

    Booster bst2 = XGBoost.loadModel(temp.getAbsolutePath());
    assert (Arrays.equals(bst2.toByteArray(), booster.toByteArray()));
    float[][] predicts2 = bst2.predict(testMat, true, 0);
    TestCase.assertTrue(eval.eval(predicts2, testMat) < 0.1f);
  }

  @Test
  public void saveLoadModelWithStream() throws XGBoostError, IOException {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");

    Booster booster = trainBooster(trainMat, testMat);

    ByteArrayOutputStream output = new ByteArrayOutputStream();
    booster.saveModel(output);
    IEvaluation eval = new EvalError();
    Booster loadedBooster = XGBoost.loadModel(new ByteArrayInputStream(output.toByteArray()));
    float originalPredictError = eval.eval(booster.predict(testMat, true), testMat);
    TestCase.assertTrue("originalPredictErr:" + originalPredictError,
            originalPredictError < 0.1f);
    float loadedPredictError = eval.eval(loadedBooster.predict(testMat, true), testMat);
    TestCase.assertTrue("loadedPredictErr:" + loadedPredictError, loadedPredictError < 0.1f);
  }

  private static class IncreasingEval implements IEvaluation {
    private int value = 1;

    @Override
    public String getMetric() {
      return "inc";
    }

    @Override
    public float eval(float[][] predicts, DMatrix dmat) {
      return value++;
    }
  }

  @Test
  public void testDescendMetricsWithBoundaryCondition() {
    // maximize_evaluation_metrics = false
    int totalIterations = 11;
    int earlyStoppingRound = 10;
    float[][] metrics = new float[1][totalIterations];
    for (int i = 0; i < totalIterations; i++) {
      metrics[0][i] = i;
    }
    int bestIteration = 0;

    for (int itr = 0; itr < totalIterations; itr++) {
      boolean es = XGBoost.shouldEarlyStop(earlyStoppingRound, itr, bestIteration);
      if (itr == totalIterations - 1) {
        TestCase.assertTrue(es);
      } else {
        TestCase.assertFalse(es);
      }
    }
  }

  @Test
  public void testEarlyStoppingForMultipleMetrics() {
    // maximize_evaluation_metrics = true
    int earlyStoppingRound = 3;
    int totalIterations = 5;
    int numOfMetrics = 3;
    float[][] metrics = new float[numOfMetrics][totalIterations];
    // Only assign metric values to the first dataset, zeros for other datasets
    for (int i = 0; i < numOfMetrics; i++) {
      for (int j = 0; j < totalIterations; j++) {
        metrics[0][j] = j;
      }
    }
    int bestIteration;

    for (int i = 0; i < totalIterations; i++) {
      bestIteration = i;
      boolean es = XGBoost.shouldEarlyStop(earlyStoppingRound, i, bestIteration);
      TestCase.assertFalse(es);
    }

    // when we have multiple datasets, only the last one was used to determinate early stop
    // Here we changed the metric of the first dataset, it doesn't have any effect to the final result
    for (int i = 0; i < totalIterations; i++) {
      metrics[0][i] = totalIterations - i;
    }
    for (int i = 0; i < totalIterations; i++) {
      bestIteration = i;
      boolean es = XGBoost.shouldEarlyStop(earlyStoppingRound, i, bestIteration);
      TestCase.assertFalse(es);
    }

    // Now assign metric values to the last dataset.
    for (int i = 0; i < totalIterations; i++) {
      metrics[2][i] = totalIterations - i;
    }
    bestIteration = 0;

    for (int i = 0; i < totalIterations; i++) {
      // if any metrics off, we need to stop
      boolean es = XGBoost.shouldEarlyStop(earlyStoppingRound, i, bestIteration);
      if (i >= earlyStoppingRound) {
        TestCase.assertTrue(es);
      } else {
        TestCase.assertFalse(es);
      }
    }
  }

  @Test
  public void testDescendMetrics() {
    // maximize_evaluation_metrics = false
    int totalIterations = 10;
    int earlyStoppingRounds = 5;
    float[][] metrics = new float[1][totalIterations];
    for (int i = 0; i < totalIterations; i++) {
      metrics[0][i] = i;
    }
    int bestIteration = 0;

    boolean es = XGBoost.shouldEarlyStop(earlyStoppingRounds, totalIterations - 1, bestIteration);
    TestCase.assertTrue(es);
    for (int i = 0; i < totalIterations; i++) {
      metrics[0][i] = totalIterations - i;
    }
    bestIteration = totalIterations - 1;

    es = XGBoost.shouldEarlyStop(earlyStoppingRounds, totalIterations - 1, bestIteration);
    TestCase.assertFalse(es);

    for (int i = 0; i < totalIterations; i++) {
      metrics[0][i] = totalIterations - i;
    }
    metrics[0][4] = 1;
    metrics[0][9] = 5;

    bestIteration = 4;

    es = XGBoost.shouldEarlyStop(earlyStoppingRounds, totalIterations - 1, bestIteration);
    TestCase.assertTrue(es);
  }

  @Test
  public void testAscendMetricsWithBoundaryCondition() {
    // maximize_evaluation_metrics = true
    int totalIterations = 11;
    int earlyStoppingRounds = 10;
    float[][] metrics = new float[1][totalIterations];
    for (int i = 0; i < totalIterations; i++) {
      metrics[0][i] = totalIterations - i;
    }
    int bestIteration = 0;

    for (int itr = 0; itr < totalIterations; itr++) {
      boolean es = XGBoost.shouldEarlyStop(earlyStoppingRounds, itr, bestIteration);
      if (itr == totalIterations - 1) {
        TestCase.assertTrue(es);
      } else {
        TestCase.assertFalse(es);
      }
    }
  }

  @Test
  public void testAscendMetrics() {
    // maximize_evaluation_metrics = true
    int totalIterations = 10;
    int earlyStoppingRounds = 5;
    float[][] metrics = new float[1][totalIterations];
    for (int i = 0; i < totalIterations; i++) {
      metrics[0][i] = totalIterations - i;
    }
    int bestIteration = 0;

    boolean es = XGBoost.shouldEarlyStop(earlyStoppingRounds, totalIterations - 1, bestIteration);
    TestCase.assertTrue(es);
    for (int i = 0; i < totalIterations; i++) {
      metrics[0][i] = i;
    }
    bestIteration = totalIterations - 1;

    es = XGBoost.shouldEarlyStop(earlyStoppingRounds, totalIterations - 1, bestIteration);
    TestCase.assertFalse(es);

    for (int i = 0; i < totalIterations; i++) {
      metrics[0][i] = i;
    }
    metrics[0][4] = 9;
    metrics[0][9] = 4;

    bestIteration = 4;

    es = XGBoost.shouldEarlyStop(earlyStoppingRounds, totalIterations - 1, bestIteration);
    TestCase.assertTrue(es);
  }

  @Test
  public void testBoosterEarlyStop() throws XGBoostError, IOException {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");
    Map<String, Object> paramMap = new HashMap<String, Object>() {
      {
        put("max_depth", 3);
        put("silent", 1);
        put("objective", "binary:logistic");
        put("maximize_evaluation_metrics", "false");
      }
    };
    Map<String, DMatrix> watches = new LinkedHashMap<>();
    watches.put("training", trainMat);
    watches.put("test", testMat);

    final int round = 10;
    int earlyStoppingRound = 2;
    float[][] metrics = new float[watches.size()][round];
    XGBoost.train(trainMat, paramMap, round, watches, metrics, null, new IncreasingEval(),
            earlyStoppingRound);

    // Make sure we've stopped early.
    for (int w = 0; w < watches.size(); w++) {
      for (int r = 0; r <= earlyStoppingRound; r++) {
        TestCase.assertFalse(0.0f == metrics[w][r]);
      }
    }

    for (int w = 0; w < watches.size(); w++) {
      for (int r = earlyStoppingRound + 1; r < round; r++) {
        TestCase.assertEquals(0.0f, metrics[w][r]);
      }
    }
  }

  private void testWithQuantileHisto(DMatrix trainingSet, Map<String, DMatrix> watches, int round,
                                      Map<String, Object> paramMap, float threshold) throws XGBoostError {
    float[][] metrics = new float[watches.size()][round];
    Booster booster = XGBoost.train(trainingSet, paramMap, round, watches,
            metrics, null, null, 0);
    for (int i = 0; i < metrics.length; i++)
      for (int j = 1; j < metrics[i].length; j++) {
        TestCase.assertTrue(metrics[i][j] >= metrics[i][j - 1] ||
                Math.abs(metrics[i][j] - metrics[i][j - 1]) < 0.1);
      }
    for (int i = 0; i < metrics.length; i++)
      for (int j = 0; j < metrics[i].length; j++) {
        TestCase.assertTrue(metrics[i][j] >= threshold);
      }
    booster.dispose();
  }

  @Test
  public void testQuantileHistoDepthWise() throws XGBoostError {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");
    Map<String, Object> paramMap = new HashMap<String, Object>() {
      {
        put("max_depth", 3);
        put("silent", 1);
        put("objective", "binary:logistic");
        put("tree_method", "hist");
        put("grow_policy", "depthwise");
        put("eval_metric", "auc");
      }
    };
    Map<String, DMatrix> watches = new HashMap<>();
    watches.put("training", trainMat);
    watches.put("test", testMat);
    testWithQuantileHisto(trainMat, watches, 10, paramMap, 0.95f);
  }

  @Test
  public void testQuantileHistoLossGuide() throws XGBoostError {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");
    Map<String, Object> paramMap = new HashMap<String, Object>() {
      {
        put("max_depth", 0);
        put("silent", 1);
        put("objective", "binary:logistic");
        put("tree_method", "hist");
        put("grow_policy", "lossguide");
        put("max_leaves", 8);
        put("eval_metric", "auc");
      }
    };
    Map<String, DMatrix> watches = new HashMap<>();
    watches.put("training", trainMat);
    watches.put("test", testMat);
    testWithQuantileHisto(trainMat, watches, 10, paramMap, 0.95f);
  }

  @Test
  public void testQuantileHistoLossGuideMaxBin() throws XGBoostError {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");
    Map<String, Object> paramMap = new HashMap<String, Object>() {
      {
        put("max_depth", 0);
        put("silent", 1);
        put("objective", "binary:logistic");
        put("tree_method", "hist");
        put("grow_policy", "lossguide");
        put("max_leaves", 8);
        put("max_bin", 16);
        put("eval_metric", "auc");
      }
    };
    Map<String, DMatrix> watches = new HashMap<>();
    watches.put("training", trainMat);
    testWithQuantileHisto(trainMat, watches, 10, paramMap, 0.95f);
  }

  @Test
  public void testDumpModelJson() throws XGBoostError {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");

    Booster booster = trainBooster(trainMat, testMat);
    String[] dump = booster.getModelDump("", false, "json");
    TestCase.assertEquals("  { \"nodeid\":", dump[0].substring(0, 13));

    // test with specified feature names
    String[] featureNames = new String[126];
    for(int i = 0; i < 126; i++) featureNames[i] = "test_feature_name_" + i;
    dump = booster.getModelDump(featureNames, false, "json");
    TestCase.assertTrue(dump[0].contains("test_feature_name_"));
  }

  @Test
  public void testGetFeatureScore() throws XGBoostError {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");

    Booster booster = trainBooster(trainMat, testMat);
    String[] featureNames = new String[126];
    for(int i = 0; i < 126; i++) featureNames[i] = "test_feature_name_" + i;
    Map<String, Integer> scoreMap = booster.getFeatureScore(featureNames);
    for (String fName: scoreMap.keySet()) TestCase.assertTrue(fName.startsWith("test_feature_name_"));
  }

  @Test
  public void testGetFeatureImportanceGain() throws XGBoostError {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");

    Booster booster = trainBooster(trainMat, testMat);
    String[] featureNames = new String[126];
    for(int i = 0; i < 126; i++) featureNames[i] = "test_feature_name_" + i;
    Map<String, Double> scoreMap = booster.getScore(featureNames, "gain");
    for (String fName: scoreMap.keySet()) TestCase.assertTrue(fName.startsWith("test_feature_name_"));
  }

  @Test
  public void testGetFeatureImportanceTotalGain() throws XGBoostError {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");

    Booster booster = trainBooster(trainMat, testMat);
    String[] featureNames = new String[126];
    for(int i = 0; i < 126; i++) featureNames[i] = "test_feature_name_" + i;
    Map<String, Double> scoreMap = booster.getScore(featureNames, "total_gain");
    for (String fName: scoreMap.keySet()) TestCase.assertTrue(fName.startsWith("test_feature_name_"));
  }

  @Test
  public void testGetFeatureImportanceCover() throws XGBoostError {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");

    Booster booster = trainBooster(trainMat, testMat);
    String[] featureNames = new String[126];
    for(int i = 0; i < 126; i++) featureNames[i] = "test_feature_name_" + i;
    Map<String, Double> scoreMap = booster.getScore(featureNames, "cover");
    for (String fName: scoreMap.keySet()) TestCase.assertTrue(fName.startsWith("test_feature_name_"));
  }

  @Test
  public void testGetFeatureImportanceTotalCover() throws XGBoostError {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");

    Booster booster = trainBooster(trainMat, testMat);
    String[] featureNames = new String[126];
    for(int i = 0; i < 126; i++) featureNames[i] = "test_feature_name_" + i;
    Map<String, Double> scoreMap = booster.getScore(featureNames, "total_cover");
    for (String fName: scoreMap.keySet()) TestCase.assertTrue(fName.startsWith("test_feature_name_"));
  }

  @Test
  public void testQuantileHistoDepthwiseMaxDepth() throws XGBoostError {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    Map<String, Object> paramMap = new HashMap<String, Object>() {
      {
        put("max_depth", 3);
        put("silent", 1);
        put("objective", "binary:logistic");
        put("tree_method", "hist");
        put("grow_policy", "depthwise");
        put("eval_metric", "auc");
      }
    };
    Map<String, DMatrix> watches = new HashMap<>();
    watches.put("training", trainMat);
    testWithQuantileHisto(trainMat, watches, 10, paramMap, 0.95f);
  }

  @Test
  public void testQuantileHistoDepthwiseMaxDepthMaxBin() throws XGBoostError {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");
    Map<String, Object> paramMap = new HashMap<String, Object>() {
      {
        put("max_depth", 3);
        put("silent", 1);
        put("objective", "binary:logistic");
        put("tree_method", "hist");
        put("max_bin", 2);
        put("grow_policy", "depthwise");
        put("eval_metric", "auc");
      }
    };
    Map<String, DMatrix> watches = new HashMap<>();
    watches.put("training", trainMat);
    testWithQuantileHisto(trainMat, watches, 10, paramMap, 0.95f);
  }

  /**
   * test cross valiation
   *
   * @throws XGBoostError
   */
  @Test
  public void testCV() throws XGBoostError {
    //load train mat
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");

    //set params
    Map<String, Object> param = new HashMap<String, Object>() {
      {
        put("eta", 1.0);
        put("max_depth", 3);
        put("silent", 1);
        put("nthread", 6);
        put("objective", "binary:logistic");
        put("gamma", 1.0);
        put("eval_metric", "error");
      }
    };

    //do 5-fold cross validation
    int round = 2;
    int nfold = 5;
    String[] evalHist = XGBoost.crossValidation(trainMat, param, round, nfold, null, null, null);
  }

  /**
   * test train from existing model
   *
   * @throws XGBoostError
   */
  @Test
  public void testTrainFromExistingModel() throws XGBoostError, IOException {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");
    IEvaluation eval = new EvalError();

    Map<String, Object> paramMap = new HashMap<String, Object>() {
      {
        put("eta", 1.0);
        put("max_depth", 2);
        put("silent", 1);
        put("objective", "binary:logistic");
      }
    };

    //set watchList
    HashMap<String, DMatrix> watches = new HashMap<String, DMatrix>();

    watches.put("train", trainMat);
    watches.put("test", testMat);

    // Train without saving temp booster
    int round = 4;
    Booster booster1 = XGBoost.train(trainMat, paramMap, round, watches, null, null, null, 0);
    float booster1error = eval.eval(booster1.predict(testMat, true, 0), testMat);

    // Train with temp Booster
    round = 2;
    Booster tempBooster = XGBoost.train(trainMat, paramMap, round, watches, null, null, null, 0);
    float tempBoosterError = eval.eval(tempBooster.predict(testMat, true, 0), testMat);

    // Save tempBooster to bytestream and load back
    int prevVersion = tempBooster.getVersion();
    ByteArrayInputStream in = new ByteArrayInputStream(tempBooster.toByteArray());
    tempBooster = XGBoost.loadModel(in);
    in.close();
    tempBooster.setVersion(prevVersion);

    // Continue training using tempBooster
    round = 4;
    Booster booster2 = XGBoost.train(trainMat, paramMap, round, watches, null, null, null, 0, tempBooster);
    float booster2error = eval.eval(booster2.predict(testMat, true, 0), testMat);
    TestCase.assertTrue(booster1error == booster2error);
    TestCase.assertTrue(tempBoosterError > booster2error);
  }

  /**
   * test set/get attributes to/from a booster
   *
   * @throws XGBoostError
   */
  @Test
  public void testSetAndGetAttrs() throws XGBoostError {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");

    Booster booster = trainBooster(trainMat, testMat);
    booster.setAttr("testKey1", "testValue1");
    TestCase.assertEquals(booster.getAttr("testKey1"), "testValue1");
    booster.setAttr("testKey1", "testValue2");
    TestCase.assertEquals(booster.getAttr("testKey1"), "testValue2");

    booster.setAttrs(new HashMap<String, String>(){{
      put("aa", "AA");
      put("bb", "BB");
      put("cc", "CC");
    }});

    Map<String, String> attr = booster.getAttrs();
    TestCase.assertEquals(attr.size(), 4);
    TestCase.assertEquals(attr.get("testKey1"), "testValue2");
    TestCase.assertEquals(attr.get("aa"), "AA");
    TestCase.assertEquals(attr.get("bb"), "BB");
    TestCase.assertEquals(attr.get("cc"), "CC");
  }

  /**
   * test get number of features from a booster
   *
   * @throws XGBoostError
   */
  @Test
  public void testGetNumFeature() throws XGBoostError {
    DMatrix trainMat = new DMatrix("../../demo/data/agaricus.txt.train");
    DMatrix testMat = new DMatrix("../../demo/data/agaricus.txt.test");

    Booster booster = trainBooster(trainMat, testMat);
    TestCase.assertEquals(booster.getNumFeature(), 127);
  }
}
