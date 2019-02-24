/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.apache.arrow.vector;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.apache.arrow.memory.BufferAllocator;
import org.apache.arrow.memory.RootAllocator;

import org.apache.arrow.vector.VarCharVector;
import org.apache.arrow.vector.util.TransferPair;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;

public class TestSplitAndTransfer {
  private BufferAllocator allocator;

  @Before
  public void init() {
    allocator = new RootAllocator(Long.MAX_VALUE);
  }
  
  @After
  public void terminate() throws Exception {
    allocator.close();
  }
  
  @Test /* VarCharVector */
  public void test() throws Exception {
    try (final VarCharVector varCharVector = new VarCharVector("myvector", allocator)) {
      varCharVector.allocateNew(10000, 1000);
  
      final int valueCount = 500;
      final String[] compareArray = new String[valueCount];
  
      for (int i = 0; i < valueCount; i += 3) {
        final String s = String.format("%010d", i);
        varCharVector.set(i, s.getBytes());
        compareArray[i] = s;
      }
      varCharVector.setValueCount(valueCount);
  
      final TransferPair tp = varCharVector.getTransferPair(allocator);
      final VarCharVector newVarCharVector = (VarCharVector) tp.getTo();
      final int[][] startLengths = {{0, 201}, {201, 0}, {201, 200}, {401, 99}};
  
      for (final int[] startLength : startLengths) {
        final int start = startLength[0];
        final int length = startLength[1];
        tp.splitAndTransfer(start, length);
        for (int i = 0; i < length; i++) {
          final boolean expectedSet = ((start + i) % 3) == 0;
          if (expectedSet) {
            final byte[] expectedValue = compareArray[start + i].getBytes();
            assertFalse(newVarCharVector.isNull(i));
            assertArrayEquals(expectedValue, newVarCharVector.get(i));
          } else {
            assertTrue(newVarCharVector.isNull(i));
          }
        }
        newVarCharVector.clear();
      }
    }
  }

  @Test
  public void testMemoryConstrainedTransfer() {
    try (final VarCharVector varCharVector = new VarCharVector("myvector", allocator)) {
      allocator.setLimit(32768); /* set limit of 32KB */

      varCharVector.allocateNew(10000, 1000);

      final int valueCount = 1000;

      for (int i = 0; i < valueCount; i += 3) {
        final String s = String.format("%010d", i);
        varCharVector.set(i, s.getBytes());
      }
      varCharVector.setValueCount(valueCount);

      final TransferPair tp = varCharVector.getTransferPair(allocator);
      final VarCharVector newVarCharVector = (VarCharVector) tp.getTo();
      final int[][] startLengths = {{0, 700}, {700, 299}};

      for (final int[] startLength : startLengths) {
        final int start = startLength[0];
        final int length = startLength[1];
        tp.splitAndTransfer(start, length);
        newVarCharVector.clear();
      }
    }
  }
}
