/*  YUView - YUV player with advanced analytics toolset
*   Copyright (C) 2015  Institut für Nachrichtentechnik
*                       RWTH Aachen University, GERMANY
*
*   YUView is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   YUView is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with YUView.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "videoHandlerDifference.h"

#include <algorithm>
#include "signalsSlots.h"

videoHandlerDifference::videoHandlerDifference() : videoHandler()
{
  markDifference = false;
  amplificationFactor = 1;
  codingOrder = CodingOrder_HEVC;
}

void videoHandlerDifference::loadFrame(int frameIndex)
{
  //qDebug() << "videoHandlerDifference::loadFrame " << frameIndex;

  // Calculate the difference between the inputVideos
  if (!inputsValid())
    return;
  
  differenceInfoList.clear();
  currentFrame = inputVideo[0]->calculateDifference(inputVideo[1], frameIndex, differenceInfoList, amplificationFactor, markDifference);
  currentFrameIdx = frameIndex;

  // The difference has been calculated and is ready to draw. Now the first difference position can be calculated.
  emit signalHandlerChanged(false, false);
}

bool videoHandlerDifference::inputsValid() const
{
  if (inputVideo[0] == NULL || inputVideo[1] == NULL)
    return false;
  
  if (!inputVideo[0]->isFormatValid() || !inputVideo[1]->isFormatValid())
    return false;

  return true;
}

void videoHandlerDifference::setInputVideos(frameHandler *childVideo0, frameHandler *childVideo1)
{
  if (inputVideo[0] != childVideo0 || inputVideo[1] != childVideo1)
  {
    // Something changed
    inputVideo[0] = childVideo0;
    inputVideo[1] = childVideo1;

    if (inputsValid())
    {
      // We have two valid video "children"

      // Get the frame size of the difference (min in x and y direction), and set it.
      QSize size0 = inputVideo[0]->getFrameSize();
      QSize size1 = inputVideo[1]->getFrameSize();
      QSize diffSize = QSize( std::min(size0.width(), size1.width()), std::min(size0.height(), size1.height()) );
      setFrameSize(diffSize);
    }

    // If something changed, we might need a redraw
    emit signalHandlerChanged(true, false);
  }
}

ValuePairList videoHandlerDifference::getPixelValues(const QPoint &pixelPos, int frameIdx, frameHandler *item2)
{
  Q_UNUSED(item2);

  if (!inputsValid())
    return ValuePairList();

  return inputVideo[0]->getPixelValues(pixelPos, frameIdx, inputVideo[1]);
}

void videoHandlerDifference::drawPixelValues(QPainter *painter, const int frameIdx, const QRect &videoRect, const double zoomFactor, frameHandler *item2, const bool markDifference)
{
  Q_UNUSED(item2);
  Q_UNUSED(markDifference);

  if (!inputsValid())
    return;

  inputVideo[0]->drawPixelValues(painter, frameIdx, videoRect, zoomFactor, inputVideo[1], this->markDifference);
}

QLayout *videoHandlerDifference::createDifferenceHandlerControls()
{

  // Absolutely always only call this function once!
  assert(!ui.created());

  ui.setupUi();

  // Set all the values of the properties widget to the values of this class
  ui.markDifferenceCheckBox->setChecked( markDifference );
  ui.amplificationFactorSpinBox->setValue( amplificationFactor );
  ui.codingOrderComboBox->addItems( QStringList() << "HEVC" );
  ui.codingOrderComboBox->setCurrentIndex( (int)codingOrder );
   
  // Connect all the change signals from the controls to "connectWidgetSignals()"
  connect(ui.markDifferenceCheckBox, &QCheckBox::stateChanged, this, &videoHandlerDifference::slotDifferenceControlChanged);
  connect(ui.codingOrderComboBox, QComboBox_currentIndexChanged_int, this, &videoHandlerDifference::slotDifferenceControlChanged);
  connect(ui.amplificationFactorSpinBox, QSpinBox_valueChanged_int, this, &videoHandlerDifference::slotDifferenceControlChanged);
    
  return ui.topVBoxLayout;
}

void videoHandlerDifference::slotDifferenceControlChanged()
{
  // The control that caused the slot to be called
  QObject *sender = QObject::sender();

  if (sender == ui.markDifferenceCheckBox)
  {
    markDifference = ui.markDifferenceCheckBox->isChecked();

    // Set the current frame in the buffer to be invalid and emit the signal that something has changed
    currentFrameIdx = -1;
    emit signalHandlerChanged(true, false);
  }
  else if (sender == ui.codingOrderComboBox)
  {
    codingOrder = (CodingOrder)ui.codingOrderComboBox->currentIndex();

     // The calculation of the first difference in coding order changed but no redraw is necessary
    emit signalHandlerChanged(false, false);
  }
  else if (sender == ui.amplificationFactorSpinBox)
  {
    amplificationFactor = ui.amplificationFactorSpinBox->value();

    // Set the current frame in the buffer to be invalid and emit the signal that something has changed
    currentFrameIdx = -1;
    emit signalHandlerChanged(true, false);
  }
}

void videoHandlerDifference::reportFirstDifferencePosition(QList<infoItem> &infoList) const
{
  if (!inputsValid())
    return;

  QImage diffImg = currentFrame.toImage();
  if (diffImg.width() != frameSize.width() || diffImg.height() != frameSize.height())
    return;

  if (codingOrder == CodingOrder_HEVC)
  {
    // Assume the following:
    // - The picture is split into LCUs of 64x64 pixels which are scanned in raster scan
    // - Each LCU is scanned in a hierarchical tree until the smalles unit size (4x4 pixels) is reached
    // This is exactly what we are going to do here now

    int widthLCU  = (frameSize.width()  + 63) / 64;  // Round up
    int heightLCU = (frameSize.height() + 63) / 64;

    for (int y = 0; y < heightLCU; y++)
    {
      for (int x = 0; x < widthLCU; x++)
      {
        // Now take the tree approach
        int firstX, firstY, partIndex = 0;
        if (hierarchicalPosition( x*64, y*64, 64, firstX, firstY, partIndex, diffImg ))
        {
          // We found a difference in this block
          infoList.append( infoItem("First Difference LCU", QString::number(y * widthLCU + x)) );
          infoList.append( infoItem("First Difference X", QString::number(firstX)) );
          infoList.append( infoItem("First Difference Y", QString::number(firstY)) );
          infoList.append( infoItem("First Difference partIndex", QString::number(partIndex)) );
          return;
        }
      }
    }
  }

  // No difference was found
  infoList.append( infoItem("Difference", "Frames are identical") );
}

bool videoHandlerDifference::hierarchicalPosition( int x, int y, int blockSize, int &firstX, int &firstY, int &partIndex, const QImage &diffImg ) const
{
  if (x >= frameSize.width() || y >= frameSize.height())
    // This block is entirely outside of the picture
    return false;

  if (blockSize == 4)
  {
    // Check for a difference
    for (int subX=x; subX < x+4; subX++)
    {
      for (int subY=y; subY < y+4; subY++)
      {
        QRgb rgb = diffImg.pixel( QPoint(subX, subY) );

        if (markDifference)
        {
          // Black means no difference
          if (qRed(rgb) != 0 || qGreen(rgb) != 0 || qBlue(rgb) != 0)
          {
            // First difference found
            firstX = x;
            firstY = y;
            return true;
          }
        }
        else
        {
          // TODO: Double check if this is always true
          // Do other values also converti to RGB(130,130,130) ?
          // What about ten bit input material
          int red = qRed(rgb);
          int green = qGreen(rgb);
          int blue = qBlue(rgb);

          if (red != 130 || green != 130 || blue != 130)
          {
            // First difference found
            firstX = x;
            firstY = y;
            return true;
          }
        }
      }
    }

    // No difference found in this block. Count the number of 4x4 blocks scanned (that is the partIndex)
    partIndex++;
  }
  else
  {
    // Walk further into the hierarchie
    if ( hierarchicalPosition(x              , y              , blockSize/2, firstX, firstY, partIndex, diffImg) )
      return true;
    if ( hierarchicalPosition(x + blockSize/2, y              , blockSize/2, firstX, firstY, partIndex, diffImg) )
      return true;
    if ( hierarchicalPosition(x              , y + blockSize/2, blockSize/2, firstX, firstY, partIndex, diffImg) )
      return true;
    if ( hierarchicalPosition(x + blockSize/2, y + blockSize/2, blockSize/2, firstX, firstY, partIndex, diffImg) )
      return true;
  }
  return false;
}